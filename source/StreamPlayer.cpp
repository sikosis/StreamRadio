/*
 * Copyright (C) 2017 Kai Niessen <kai.niessen@online.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#include <Catalog.h>
#include <MediaDecoder.h>
#include <MediaFile.h>
#include <MediaTrack.h>

#include <string.h>

#include "Debug.h"
#include "StreamIO.h"
#include "StreamPlayer.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StreamPlayer"


static const int32 kBufferFlushInterval = 512;
static const bigtime_t kPlaybackStallTimeout = 15000000;
static const bigtime_t kNetworkStallTimeout = 30000000;
static const bigtime_t kWatchdogInterval = 500000;
static const off_t kMinimumBufferTarget = 32 * 1024;
static const off_t kMaximumBufferTarget = 256 * 1024;


StreamPlayer::StreamPlayer(Station* station, BLooper* notify)
	: BLocker("StreamPlayer"),
	  fStation(station),
	  fNotify(notify),
	  fNotificationsEnabled(notify != NULL ? 1 : 0),
	  fMediaFile(NULL),
	  fStream(NULL),
	  fPlayer(NULL),
	  fState(StreamPlayer::Stopped),
	  fInitStatus(B_NO_INIT),
	  fStopRequested(0),
	  fReconnectRequested(0),
	  fReconnectAttempts(0),
	  fDecodeFailures(0),
	  fRebuffering(0),
	  fProgressLock("StreamPlayer progress"),
	  fLastPlaybackProgress(system_time()),
	  fLastNetworkProgress(system_time()),
	  fCodecInfo(),
	  fDecodedFormat(),
	  fHeader(),
	  fInfo(),
	  fFlushCount(0),
	  fPlayThread(-1),
	  fWatchdogThread(-1)
{
	TRACE("Trying to set player for stream %s\n", station->StreamUrl().UrlString().String());

	fStream = new (std::nothrow)
		StreamIO(station, notify, &StreamPlayer::_NetworkProgressFunc, this);
	if (fStream == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fInitStatus = fStream->Open();
	if (fInitStatus != B_OK) {
		MSG("Error retrieving stream from %s - %s\n", station->StreamUrl().UrlString().String(),
			strerror(fInitStatus));
		delete fStream;
		fStream = NULL;
		return;
	}
}


StreamPlayer::~StreamPlayer()
{
	Stop();
	if (fWatchdogThread >= B_OK && fWatchdogThread != find_thread(NULL)) {
		status_t status;
		wait_for_thread(fWatchdogThread, &status);
	}
	if (fPlayThread >= B_OK && fPlayThread != find_thread(NULL)) {
		status_t status;
		wait_for_thread(fPlayThread, &status);
	}

	delete fPlayer;
	delete fMediaFile;
	delete fStream;
}


status_t
StreamPlayer::Play()
{
	switch (atomic_get(&fState)) {
		case StreamPlayer::Playing:
		case StreamPlayer::Buffering:
		case StreamPlayer::InActive:
			break;

		case StreamPlayer::Stopped:
		{
			if (fStream == NULL)
				return fInitStatus;

			thread_id playThreadId = spawn_thread((thread_func)StreamPlayer::_StartPlayThreadFunc,
				fStation->Name()->String(), B_NORMAL_PRIORITY, this);
			if (playThreadId < B_OK)
				return playThreadId;

			thread_id watchdogThreadId = spawn_thread(
				(thread_func)StreamPlayer::_WatchdogThreadFunc, "StreamRadio watchdog",
				B_NORMAL_PRIORITY, this);

			fPlayThread = playThreadId;
			if (watchdogThreadId >= B_OK)
				fWatchdogThread = watchdogThreadId;
			else
				MSG("Could not create stream watchdog: %s\n", strerror(watchdogThreadId));

			atomic_set(&fStopRequested, 0);
			_MarkPlaybackProgress();
			_SetState(StreamPlayer::Buffering);

			if (watchdogThreadId >= B_OK) {
				status_t watchdogStatus = resume_thread(watchdogThreadId);
				if (watchdogStatus != B_OK) {
					fWatchdogThread = -1;
					MSG("Could not start stream watchdog: %s\n", strerror(watchdogStatus));
				}
			}

			status_t status = resume_thread(playThreadId);
			if (status != B_OK) {
				atomic_set(&fStopRequested, 1);
				fPlayThread = -1;
				_SetState(StreamPlayer::Stopped);
				return status;
			}

			break;
		}
	}

	return B_OK;
}


status_t
StreamPlayer::Stop()
{
	atomic_set(&fStopRequested, 1);
	atomic_set(&fRebuffering, 0);

	switch (atomic_get(&fState)) {
		case StreamPlayer::Buffering:
		case StreamPlayer::Playing:
		{
			Lock();

			if (fPlayer) {
				fPlayer->SetHasData(false);
				fPlayer->Stop(false, true);
				delete fPlayer;
				fPlayer = NULL;
			}

			if (fMediaFile) {
				fMediaFile->CloseFile();
				delete fMediaFile;
				fMediaFile = NULL;
			}

			Unlock();
			_SetState(StreamPlayer::Stopped);

			break;
		}

		case StreamPlayer::Stopped:
		case StreamPlayer::InActive:
			break;
	}
	return B_OK;
}


float
StreamPlayer::Volume()
{
	if (fPlayer != NULL) {
		media_node node;
		int32 paramID;
		float minDB, maxDB;
		fPlayer->GetVolumeInfo(&node, &paramID, &minDB, &maxDB);

		return (fPlayer->VolumeDB(false) - minDB) / (maxDB - minDB);
	} else
		return 0;
}


void
StreamPlayer::SetVolume(float volume)
{
	if (fPlayer != NULL) {
		media_node node;
		int32 paramID;
		float minDB, maxDB;
		fPlayer->GetVolumeInfo(&node, &paramID, &minDB, &maxDB);
		fPlayer->SetVolumeDB(minDB + volume * (maxDB - minDB));
	}
}


void
StreamPlayer::_MarkPlaybackProgress()
{
	if (fProgressLock.Lock()) {
		fLastPlaybackProgress = system_time();
		fProgressLock.Unlock();
	}
}


void
StreamPlayer::_MarkNetworkProgress()
{
	if (fProgressLock.Lock()) {
		fLastNetworkProgress = system_time();
		fProgressLock.Unlock();
	}
}


bigtime_t
StreamPlayer::_LastPlaybackProgress()
{
	bigtime_t progress = 0;
	if (fProgressLock.Lock()) {
		progress = fLastPlaybackProgress;
		fProgressLock.Unlock();
	}
	return progress;
}


bigtime_t
StreamPlayer::_LastNetworkProgress()
{
	bigtime_t progress = 0;
	if (fProgressLock.Lock()) {
		progress = fLastNetworkProgress;
		fProgressLock.Unlock();
	}
	return progress;
}


off_t
StreamPlayer::_BufferHighWater()
{
	// Keep roughly three seconds of compressed audio ready. Stations do not
	// always report a bitrate, so clamp the target to useful live-stream bounds.
	off_t target = (off_t)fStation->BitRate() * 3 / 8;
	if (target < kMinimumBufferTarget)
		return kMinimumBufferTarget;
	if (target > kMaximumBufferTarget)
		return kMaximumBufferTarget;
	return target;
}


off_t
StreamPlayer::_BufferLowWater()
{
	return _BufferHighWater() / 2;
}


void
StreamPlayer::_NetworkProgressFunc(void* cookie)
{
	StreamPlayer* player = static_cast<StreamPlayer*>(cookie);
	if (player != NULL)
		player->_MarkNetworkProgress();
}


void
StreamPlayer::_SetState(StreamPlayer::PlayState state)
{
	if (atomic_get(&fState) == state)
		return;
	atomic_set(&fState, state);

	if (atomic_get(&fNotificationsEnabled) != 0 && fNotify != NULL) {
		BMessage notification(MSG_PLAYER_STATE_CHANGED);
		BMessenger messenger(fNotify);
		notification.AddPointer("player", this);
		notification.AddInt32("state", state);
		messenger.SendMessage(&notification);
	}
}


void
StreamPlayer::_GetDecodedChunk(
	void* cookie, void* buffer, size_t size, const media_raw_audio_format& format)
{
	StreamPlayer* player = (StreamPlayer*)cookie;
	BMediaFile* fMediaFile = player->fMediaFile;
	memset(buffer, 0, size);
	if (fMediaFile == NULL || fMediaFile->CountTracks() == 0)
		return;

	if (player->fStream == NULL || player->fStream->RequestEnded()) {
		player->_ScheduleReconnect(B_IO_ERROR);
		return;
	}

	uint32 bytesPerSample = format.format & media_raw_audio_format::B_AUDIO_SIZE_MASK;
	if (format.channel_count == 0 || bytesPerSample == 0) {
		player->_ScheduleReconnect(B_MEDIA_BAD_FORMAT);
		return;
	}

	int64 requestedFrames = size / format.channel_count / bytesPerSample;
	int64 decodedFrames = requestedFrames;
	status_t status
		= fMediaFile->TrackAt(0)->ReadFrames(buffer, &decodedFrames, &player->fHeader,
			&player->fInfo);
	if (player->fStream == NULL || player->fStream->RequestEnded() || status == B_TIMED_OUT) {
		player->_ScheduleReconnect(status != B_OK ? status : B_IO_ERROR);
		return;
	}
	if (status != B_OK || decodedFrames <= 0) {
		if (atomic_add(&player->fDecodeFailures, 1) + 1 >= 3)
			player->_ScheduleReconnect(status != B_OK ? status : B_IO_ERROR);
		return;
	}

	atomic_set(&player->fDecodeFailures, 0);
	atomic_set(&player->fReconnectAttempts, 0);
	player->_MarkPlaybackProgress();

	if (++player->fFlushCount >= kBufferFlushInterval) {
		player->fFlushCount = 0;
		status_t flushStatus = player->fStream->FlushRead();
		if (flushStatus != B_OK)
			player->_ScheduleReconnect(flushStatus);
	}
}


status_t
StreamPlayer::_WatchdogThreadFunc(StreamPlayer* _this)
{
	MSG("Playback watchdog armed for %s (decoder %.0fs, network %.0fs)\n",
		_this->fStation->StreamUrl().UrlString().String(),
		kPlaybackStallTimeout / 1000000.0, kNetworkStallTimeout / 1000000.0);

	while (atomic_get(&_this->fStopRequested) == 0) {
		snooze(kWatchdogInterval);
		if (atomic_get(&_this->fStopRequested) != 0)
			break;

		bigtime_t now = system_time();
		bigtime_t networkIdleFor = now - _this->_LastNetworkProgress();
		off_t bufferHighWater = _this->_BufferHighWater();
		if (atomic_get(&_this->fRebuffering) != 0) {
			if (networkIdleFor >= kNetworkStallTimeout) {
				atomic_set(&_this->fRebuffering, 0);
				_this->_ScheduleReconnect(B_TIMED_OUT);
				continue;
			}

			_this->Lock();
			if (_this->fStream == NULL || _this->fStream->RequestEnded()) {
				_this->Unlock();
				atomic_set(&_this->fRebuffering, 0);
				_this->_ScheduleReconnect(B_IO_ERROR);
				continue;
			}
			off_t buffered = _this->fStream->BufferedBytes();
			if (buffered >= bufferHighWater && _this->fPlayer != NULL) {
				MSG("Stream buffer refilled to %.1f KiB for %s; resuming playback\n",
					buffered / 1024.0,
					_this->fStation->StreamUrl().UrlString().String());
				_this->_MarkPlaybackProgress();
				atomic_set(&_this->fRebuffering, 0);
				_this->fPlayer->SetHasData(true);
				_this->_SetState(StreamPlayer::Playing);
			}
			_this->Unlock();
			continue;
		}

		if (_this->State() != StreamPlayer::Playing)
			continue;

		_this->Lock();
		if (_this->State() != StreamPlayer::Playing
			|| atomic_get(&_this->fReconnectRequested) != 0) {
			_this->Unlock();
			continue;
		}
		off_t buffered = _this->fStream != NULL ? _this->fStream->BufferedBytes() : 0;
		if (buffered < _this->_BufferLowWater() && _this->fPlayer != NULL) {
			MSG("Stream buffer low for %s (%.1f KiB); buffering to %.1f KiB...\n",
				_this->fStation->StreamUrl().UrlString().String(), buffered / 1024.0,
				bufferHighWater / 1024.0);
			_this->fPlayer->SetHasData(false);
			atomic_set(&_this->fRebuffering, 1);
			_this->_SetState(StreamPlayer::Buffering);
			_this->Unlock();
			continue;
		}
		_this->Unlock();

		bigtime_t playbackIdleFor = now - _this->_LastPlaybackProgress();
		if (playbackIdleFor >= kPlaybackStallTimeout
			|| networkIdleFor >= kNetworkStallTimeout) {
			MSG("Stream stalled on %s (decoder %.1fs, network %.1fs); reconnecting...\n",
				_this->fStation->StreamUrl().UrlString().String(),
				playbackIdleFor / 1000000.0, networkIdleFor / 1000000.0);
			_this->_ScheduleReconnect(B_TIMED_OUT);
		}
	}

	return B_OK;
}


void
StreamPlayer::_ScheduleReconnect(status_t error)
{
	if (atomic_get(&fStopRequested) != 0 || atomic_get(&fReconnectRequested) != 0)
		return;

	bool stopAfterCleanup = atomic_get(&fReconnectAttempts) >= 3;
	if (stopAfterCleanup) {
		fInitStatus = error;
		atomic_set(&fStopRequested, 1);
		MSG("Stream recovery failed for %s after 3 attempts (%s)\n",
			fStation->StreamUrl().UrlString().String(), strerror(error));
	} else {
		atomic_add(&fReconnectAttempts, 1);
		MSG("Stream stopped for %s (%s); reconnecting (%" B_PRId32 "/3)...\n",
			fStation->StreamUrl().UrlString().String(), strerror(error),
			atomic_get(&fReconnectAttempts));
	}

	atomic_set(&fReconnectRequested, 1);
	atomic_set(&fRebuffering, 0);
	_SetState(StreamPlayer::Buffering);

	thread_id reconnectThread = spawn_thread((thread_func)StreamPlayer::_ReconnectThreadFunc,
		"StreamRadio reconnect", B_NORMAL_PRIORITY, this);
	if (reconnectThread < B_OK) {
		fInitStatus = reconnectThread;
		atomic_set(&fReconnectRequested, 0);
		_SetState(StreamPlayer::Stopped);
		return;
	}

	fPlayThread = reconnectThread;
	status_t status = resume_thread(reconnectThread);
	if (status != B_OK) {
		fInitStatus = status;
		fPlayThread = -1;
		atomic_set(&fReconnectRequested, 0);
		_SetState(StreamPlayer::Stopped);
	}
}


status_t
StreamPlayer::_ReconnectThreadFunc(StreamPlayer* _this)
{
	snooze(1000000);

	_this->Lock();
	if (_this->fPlayer != NULL) {
		_this->fPlayer->SetHasData(false);
		_this->fPlayer->Stop(true, true);
		delete _this->fPlayer;
		_this->fPlayer = NULL;
	}
	if (_this->fMediaFile != NULL) {
		_this->fMediaFile->CloseFile();
		delete _this->fMediaFile;
		_this->fMediaFile = NULL;
	}

	delete _this->fStream;
	_this->fStream = NULL;
	atomic_set(&_this->fDecodeFailures, 0);
	_this->fFlushCount = 0;

	if (atomic_get(&_this->fStopRequested) != 0) {
		_this->Unlock();
		atomic_set(&_this->fReconnectRequested, 0);
		_this->_SetState(StreamPlayer::Stopped);
		return B_CANCELED;
	}

	_this->_MarkNetworkProgress();
	_this->fStream = new (std::nothrow) StreamIO(_this->fStation, _this->fNotify,
		&StreamPlayer::_NetworkProgressFunc, _this);
	if (_this->fStream == NULL)
		_this->fInitStatus = B_NO_MEMORY;
	else
		_this->fInitStatus = _this->fStream->Open();

	status_t status = _this->fInitStatus;
	_this->Unlock();

	if (atomic_get(&_this->fStopRequested) != 0) {
		atomic_set(&_this->fReconnectRequested, 0);
		_this->_SetState(StreamPlayer::Stopped);
		return status;
	}
	if (status != B_OK) {
		atomic_set(&_this->fReconnectRequested, 0);
		_this->_ScheduleReconnect(status);
		return status;
	}

	atomic_set(&_this->fReconnectRequested, 0);
	return _StartPlayThreadFunc(_this);
}


status_t
StreamPlayer::_StartPlayThreadFunc(StreamPlayer* _this)
{
	if (_this->fStream == NULL) {
		_this->fInitStatus = B_NO_INIT;
		_this->_SetState(StreamPlayer::Stopped);
		return _this->fInitStatus;
	}

	_this->Lock();

	_this->_SetState(StreamPlayer::Buffering);
	_this->fStream->SetLimiter(0x40000);
	_this->fMediaFile = new (std::nothrow) BMediaFile(_this->fStream);

	if (_this->fMediaFile == NULL)
		_this->fInitStatus = B_NO_MEMORY;
	else
		_this->fInitStatus = _this->fMediaFile->InitCheck();
	if (_this->fInitStatus != B_OK) {
		MSG("Error creating media extractor for %s - %s\n",
			_this->fStation->StreamUrl().UrlString().String(), strerror(_this->fInitStatus));
	}

	if (_this->fInitStatus != B_OK || atomic_get(&_this->fStopRequested) != 0) {
		delete _this->fStream;
		_this->fStream = NULL;
		delete _this->fMediaFile;
		_this->fMediaFile = NULL;

		status_t status = _this->fInitStatus;
		bool shouldReconnect = status != B_OK && atomic_get(&_this->fStopRequested) == 0;
		_this->Unlock();
		if (shouldReconnect)
			_this->_ScheduleReconnect(status);
		else
			_this->_SetState(StreamPlayer::Stopped);

		return status;
	}

	_this->fStream->SetLimiter(0);

	if (_this->fMediaFile->CountTracks() == 0) {
		_this->fInitStatus = B_MEDIA_BAD_FORMAT;
		MSG("No track found inside stream %s\n",
			_this->fStation->StreamUrl().UrlString().String());
	}

	if (_this->fInitStatus != B_OK || atomic_get(&_this->fStopRequested) != 0) {
		delete _this->fMediaFile;
		_this->fMediaFile = NULL;

		status_t status = _this->fInitStatus;
		bool shouldReconnect = status != B_OK && atomic_get(&_this->fStopRequested) == 0;
		_this->Unlock();
		if (shouldReconnect)
			_this->_ScheduleReconnect(status);
		else
			_this->_SetState(StreamPlayer::Stopped);

		return status;
	}

	_this->fInitStatus = _this->fMediaFile->TrackAt(0)->GetCodecInfo(&_this->fCodecInfo);
	if (_this->fInitStatus != B_OK) {
		MSG("Could not create decoder for %s - %s\n",
			_this->fStation->StreamUrl().UrlString().String(), strerror(_this->fInitStatus));
	}

	if (_this->fInitStatus != B_OK || atomic_get(&_this->fStopRequested) != 0) {
		delete _this->fMediaFile;
		_this->fMediaFile = NULL;

		status_t status = _this->fInitStatus;
		bool shouldReconnect = status != B_OK && atomic_get(&_this->fStopRequested) == 0;
		_this->Unlock();
		if (shouldReconnect)
			_this->_ScheduleReconnect(status);
		else
			_this->_SetState(StreamPlayer::Stopped);

		return status;
	}

	_this->fInitStatus = _this->fMediaFile->TrackAt(0)->DecodedFormat(&_this->fDecodedFormat);
	if (_this->fInitStatus != B_OK) {
		MSG("Could not negotiate output format - %s\n", strerror(_this->fInitStatus));
	}

	if (_this->fInitStatus != B_OK || atomic_get(&_this->fStopRequested) != 0) {
		delete _this->fMediaFile;
		_this->fMediaFile = NULL;

		status_t status = _this->fInitStatus;
		bool shouldReconnect = status != B_OK && atomic_get(&_this->fStopRequested) == 0;
		_this->Unlock();
		if (shouldReconnect)
			_this->_ScheduleReconnect(status);
		else
			_this->_SetState(StreamPlayer::Stopped);

		return status;
	}

	TRACE("Found stream with %ld channels with %f Hz\n",
		_this->fDecodedFormat.u.raw_audio.channel_count,
		_this->fDecodedFormat.u.raw_audio.frame_rate);

	_this->fPlayer = new BSoundPlayer(&_this->fDecodedFormat.u.raw_audio,
		_this->fStation->Name()->String(), &StreamPlayer::_GetDecodedChunk, NULL, _this);
	if (_this->fPlayer == NULL) {
		_this->fInitStatus = B_NO_MEMORY;
		_this->Unlock();
		_this->_ScheduleReconnect(_this->fInitStatus);
		return _this->fInitStatus;
	}

	_this->fInitStatus = _this->fPlayer->InitCheck();
	if (_this->fInitStatus != B_OK) {
		MSG("Sound Player failed to initialize (%s)\r\n", strerror(_this->fInitStatus));
	}

	if (_this->fInitStatus != B_OK || atomic_get(&_this->fStopRequested) != 0) {
		_this->fPlayer->Stop(true, true);
		delete _this->fPlayer;
		_this->fPlayer = NULL;

		status_t status = _this->fInitStatus;
		bool shouldReconnect = status != B_OK && atomic_get(&_this->fStopRequested) == 0;
		_this->Unlock();
		if (shouldReconnect)
			_this->_ScheduleReconnect(status);
		else
			_this->_SetState(StreamPlayer::Stopped);

		return status;
	}

	_this->fPlayer->SetHasData(false);
	_this->fPlayer->Preroll();

	_this->fInitStatus = _this->fPlayer->Start();
	if (_this->fInitStatus != B_OK) {
		MSG("Sound Player failed to start (%s)\r\n", strerror(_this->fInitStatus));
	}

	if (_this->fInitStatus != B_OK || atomic_get(&_this->fStopRequested) != 0) {
		delete _this->fPlayer;
		_this->fPlayer = NULL;

		status_t status = _this->fInitStatus;
		bool shouldReconnect = status != B_OK && atomic_get(&_this->fStopRequested) == 0;
		_this->Unlock();
		if (shouldReconnect)
			_this->_ScheduleReconnect(status);
		else
			_this->_SetState(StreamPlayer::Stopped);

		return status;
	}

	if (atomic_get(&_this->fReconnectAttempts) > 0) {
		MSG("Reconnected to stream %s\n",
			_this->fStation->StreamUrl().UrlString().String());
	}
	off_t buffered = _this->fStream->BufferedBytes();
	off_t bufferTarget = _this->_BufferHighWater();
	if (buffered < bufferTarget) {
		MSG("Building stream buffer for %s (%.1f of %.1f KiB)...\n",
			_this->fStation->StreamUrl().UrlString().String(), buffered / 1024.0,
			bufferTarget / 1024.0);
		atomic_set(&_this->fRebuffering, 1);
		_this->_SetState(StreamPlayer::Buffering);
	} else {
		_this->fPlayer->SetHasData(true);
		_this->_MarkPlaybackProgress();
		_this->_SetState(StreamPlayer::Playing);
	}
	_this->Unlock();

	return _this->fInitStatus;
}
