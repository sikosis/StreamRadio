/*
	Copyright (C) 2008-2010 Lukas Sommer < SommerLuk at gmail dot com >
	Copyright (C) 2008-2026 Kai Niessen
	Copyright (C) 2020 Jacob Secunda

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2 of
	the License or (at your option) version 3 or any later version
	accepted by the membership of KDE e.V. (or its successor approved
	by the membership of KDE e.V.), which shall act as a proxy
	defined in Section 14 of version 3 of the license.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#include "MainWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <Dragger.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <String.h>
#include <Url.h>
#include <View.h>

#include <time.h>

#include "RadioApp.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


MainWindow::MainWindow()
	: BWindow(BRect(0, 0, 400, 200), B_TRANSLATE_SYSTEM_NAME("StreamRadio"), B_DOCUMENT_WINDOW, 0),
	  fContentSplit(NULL),
	  fStationFinder(NULL),
	  fViewMode(VIEW_MODE_NORMAL),
	  fNormalLook(B_DOCUMENT_WINDOW_LOOK),
	  fNormalStationListCollapsed(false),
	  fNormalStationPanelCollapsed(false),
	  fSleepTimerRunner(NULL),
	  fSleepTimerWindow(NULL)
{
	fSettings = &((RadioApp*)be_app)->Settings;

	fAllowParallelPlayback = fSettings->GetAllowParallelPlayback();
	fMenuParallelPlayback = new BMenuItem(
		B_TRANSLATE("Allow parallel playback"), new BMessage(MSG_PARALLEL_PLAYBACK));
	fMenuParallelPlayback->SetMarked(fAllowParallelPlayback);
	fMenuCancelSleepTimer
		= new BMenuItem(B_TRANSLATE("Cancel sleep timer"), new BMessage(MSG_SLEEP_TIMER_CANCEL));
	fMenuCancelSleepTimer->SetEnabled(false);
	fMenuMiniPlayer
		= new BMenuItem(B_TRANSLATE("Mini Player"), new BMessage(MSG_VIEW_MINI_PLAYER));
	fMenuHeightMax
		= new BMenuItem(B_TRANSLATE("Height Max"), new BMessage(MSG_VIEW_HEIGHT_MAX));
	fMenuFullscreen
		= new BMenuItem(B_TRANSLATE("Fullscreen"), new BMessage(MSG_VIEW_FULLSCREEN));

	fMainMenu = new BMenuBar(Bounds(), "MainMenu");
	BLayoutBuilder::Menu<>(fMainMenu)
		.AddMenu(B_TRANSLATE("App"))
		.AddItem(fMenuParallelPlayback)
		.AddSeparator()
		.AddItem(B_TRANSLATE("Sleep timer" B_UTF8_ELLIPSIS), MSG_SLEEP_TIMER_OPEN)
		.AddItem(fMenuCancelSleepTimer)
		.AddSeparator()
		.AddItem(B_TRANSLATE("Help" B_UTF8_ELLIPSIS), MSG_HELP)
		.AddItem(B_TRANSLATE("About"), B_ABOUT_REQUESTED)
		.AddSeparator()
		.AddItem(B_TRANSLATE("Quit"), B_QUIT_REQUESTED, 'Q')
		.End()
		.AddMenu(B_TRANSLATE("Station"))
		.AddItem(B_TRANSLATE("Paste Shoutcast URL"), MSG_PASTE_URL, 'V')
		.AddItem(B_TRANSLATE("Check station"), MSG_CHECK)
		.AddItem(B_TRANSLATE("Remove station"), MSG_REMOVE, 'R')
		.End()
		.AddMenu(B_TRANSLATE("Search"))
		.AddItem(B_TRANSLATE("Find stations" B_UTF8_ELLIPSIS), MSG_SEARCH, 'S')
		.End()
		.AddMenu(B_TRANSLATE("View"))
		.AddItem(fMenuMiniPlayer)
		.AddItem(fMenuHeightMax)
		.AddItem(fMenuFullscreen)
		.End()
		.End();

	fStationList = new StationListView(true);
	BScrollView* stationScroll = new BScrollView("scrollStation", fStationList, 0, false, true);

	fStationPanel = new StationPanel(this);
	fContentSplit = new BSplitView(B_VERTICAL, 2);

	fStatusBar = new BStringView("status", B_EMPTY_STRING);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(fMainMenu)
		.AddGroup(B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS, 0, B_USE_WINDOW_INSETS, B_USE_WINDOW_INSETS)
		.AddSplit(fContentSplit, 1.0f)
		.Add(stationScroll, 1.0f)
		.Add(fStationPanel)
		.SetCollapsible(0, true)
		.SetCollapsible(1, true)
		.End()
		.Add(fStatusBar)
		.End()
		.End();

	fStationList->Sync(fSettings->Stations);
	fStationList->SetInvocationMessage(new BMessage(MSG_INVOKE_STATION));
	fStationList->SetSelectionMessage(new BMessage(MSG_SELECT_STATION));
	fStationList->SetPlayMessage(new BMessage(MSG_INVOKE_STATION));

	fStatusBar->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_UNSET));
	fStatusBar->SetExplicitMinSize(BSize(10, B_SIZE_UNSET));

	ResizeToPreferred();
	CenterOnScreen();
	fNormalFrame = Frame();
}


MainWindow::~MainWindow()
{
	delete fSleepTimerRunner;

	for (int32 i = 0; i < fStationList->CountItems(); i++) {
		StationListViewItem* stationItem = fStationList->ItemAt(i);
		StreamPlayer* player = stationItem->Player();
		if (player != NULL) {
			player->DisableNotifications();
			player->Stop();
		}
	}
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			int32 index = 0;
			StationListViewItem* stationItem;
			BString result;
			while (message->FindRef("refs", index++, &ref) == B_OK) {
				BEntry entry(&ref);
				Station* station = Station::Load(ref.name, &entry);
				if (station) {
					Station* existingStation = fSettings->Stations->FindItem(station->Name());
					if (existingStation) {
						delete station;
						station = existingStation;
						stationItem = fStationList->Item(station);
						_Invoke(stationItem);
					} else {
						stationItem = new StationListViewItem(station);
						fStationList->AddItem(stationItem);
						result.SetToFormat(
							B_TRANSLATE("Added station %s to list"), station->Name()->String());
					}
				} else
					result.SetToFormat(
						B_TRANSLATE("File %s could not be loaded as a station"), ref.name);
				fStatusBar->SetText(result.String());
			}

			break;
		}

		case MSG_SEARCH:
			if (fStationFinder == NULL)
				fStationFinder = new StationFinderWindow(this);
			fStationFinder->Show();

			break;

		case MSG_PASTE_URL:
		{
			be_clipboard->Lock();
			BMessage* data = be_clipboard->Data();
			be_clipboard->Unlock();

			char* url;
			ssize_t numBytes;
			if (data->FindData("text/plain", B_MIME_TYPE, (const void**)&url, &numBytes) == B_OK)
				url[numBytes] = 0;

			BString sUrl(url);
			Station* station = Station::LoadIndirectUrl(sUrl);
			if (station != NULL) {
				status_t probeStatus = station->Probe();
				if (probeStatus == B_OK) {
					fSettings->Stations->AddItem(station);
					fStationList->Sync(fSettings->Stations);
					fSettings->Stations->Save();
				} else {
					BString msg;
					msg.SetToFormat(B_TRANSLATE("Station %s did not respond correctly and "
												"could not be added"),
						station->Name()->String());
					(new BAlert(B_TRANSLATE("Add station failed"), msg, B_TRANSLATE("OK")))->Go();
				}
			}

			break;
		}

		case MSG_CHECK:
		{
			StationListViewItem* stationItem
				= fStationList->ItemAt(fStationList->CurrentSelection(0));
			if (stationItem != NULL) {
				Station* station = stationItem->GetStation();
				status_t stationStatus = station->Probe();

				BString statusText;
				if (stationStatus == B_OK) {
					statusText = B_TRANSLATE("Probing station %station% successful");
				} else {
					statusText = B_TRANSLATE("Probing station %station% failed");
				}

				statusText.ReplaceFirst("%station%", station->Name()->String());
				fStatusBar->SetText(statusText);
				fStationList->Invalidate();
				fStationPanel->SetStation(stationItem);
			}
			break;
		}
		case MSG_REMOVE:
		{
			Station* station = fStationList->StationAt(fStationList->CurrentSelection(0));
			if (station != NULL) {
				fSettings->Stations->RemoveItem(station);
				fStationList->Sync(fSettings->Stations);
				fSettings->Stations->Save();
			}

			break;
		}

		case MSG_ADD_STATION:
		{
			Station* station = NULL;
			if (message->FindPointer("station", (void**)&station) == B_OK) {
				fSettings->Stations->AddItem(station);
				fStationList->Sync(fSettings->Stations);
				fSettings->Stations->Save();
			}

			break;
		}

		case MSG_SEARCH_CLOSE:
			fStationFinder = NULL;
			break;

		case MSG_SELECT_STATION:
		{
			int index = fStationList->CurrentSelection();
			if (index >= 0) {
				fStationPanel->LockLooper();
				fStationPanel->SetStation(fStationList->ItemAt(index));
				fStationPanel->UnlockLooper();
			}

			break;
		}

		case MSG_INVOKE_STATION:
		{
			int32 stationIndex = message->GetInt32("index", -1);
			StationListViewItem* stationItem = fStationList->ItemAt(stationIndex);
			_Invoke(stationItem);
			break;
		}

		case MSG_PLAYER_STATE_CHANGED:
		{
			StreamPlayer* player = NULL;
			status_t status = message->FindPointer("player", (void**)&player);

			if (player != NULL && status == B_OK) {
				StreamPlayer::PlayState state;
				status = message->FindInt32("state", (int32*)&state);

				int stationIndex = fStationList->StationIndex(player->GetStation());
				if (stationIndex >= 0) {
					StationListViewItem* stationItem = fStationList->ItemAt(stationIndex);
					stationItem->StateChanged(player->State());

					if (player->State() == StreamPlayer::Stopped) {
						fActiveStations.RemoveItem(stationItem);
						delete player;
						stationItem->SetPlayer(NULL);
					}

					if (stationIndex == fStationList->CurrentSelection()) {
						fStationPanel->LockLooper();
						fStationPanel->StateChanged(state);
						fStationPanel->UnlockLooper();
					}
				}
			}
			break;
		}

		case MSG_PLAYER_BUFFER_LEVEL:
		{
			StreamPlayer* player = NULL;
			status_t status = message->FindPointer("player", (void**)&player);
			float level = message->GetFloat("level", 0.0f);
			if (player != NULL && status == B_OK) {
				Station* station = player->GetStation();
				StationListViewItem* stationItem = fStationList->Item(station);
				if (stationItem != NULL)
					stationItem->SetFillRatio(level);
			}

			break;
		}

		case MSG_META_CHANGE:
		{
			BString meta = B_TRANSLATE("%station% now plays %title%");
			meta.ReplaceFirst(
				"%station%", message->GetString("station", B_TRANSLATE("Unknown station")));
			meta.ReplaceFirst(
				"%title%", message->GetString("streamtitle", B_TRANSLATE("unknown title")));
			fStatusBar->SetText(meta.String());

			break;
		}

		case MSG_HELP:
		{
			BUrl userguide(
				"https://github.com/sikosis/"
				"StreamRadio/blob/master/docs/userguide.md",
				true);

			userguide.OpenWithPreferredApplication(true);

			break;
		}

		case MSG_PARALLEL_PLAYBACK:
			fAllowParallelPlayback = !fAllowParallelPlayback;
			fSettings->SetAllowParallelPlayback(fAllowParallelPlayback);
			fMenuParallelPlayback->SetMarked(fAllowParallelPlayback);

			if (!fAllowParallelPlayback) {
				while (fActiveStations.CountItems() > 1)
					_TogglePlay(fActiveStations.LastItem());
			}
			break;

		case MSG_SLEEP_TIMER_OPEN:
			if (fSleepTimerWindow == NULL) {
				fSleepTimerWindow = new SleepTimerWindow(this);
				fSleepTimerWindow->Show();
			} else
				fSleepTimerWindow->ActivateWindow();
			break;

		case MSG_SLEEP_TIMER_SET:
			_SetSleepTimer((SleepTimerAction)message->GetInt32("action", SLEEP_TIMER_STOP),
				message->GetInt32("minutes", 30));
			break;

		case MSG_SLEEP_TIMER_CANCEL:
			_CancelSleepTimer();
			break;

		case MSG_SLEEP_TIMER_FIRED:
			_RunSleepTimer((SleepTimerAction)message->GetInt32("action", SLEEP_TIMER_STOP));
			break;

		case MSG_SLEEP_TIMER_WINDOW_CLOSED:
			fSleepTimerWindow = NULL;
			break;

		case MSG_VIEW_MINI_PLAYER:
			_SetViewMode(VIEW_MODE_MINI_PLAYER);
			break;

		case MSG_VIEW_HEIGHT_MAX:
			_SetViewMode(VIEW_MODE_HEIGHT_MAX);
			break;

		case MSG_VIEW_FULLSCREEN:
			_SetViewMode(VIEW_MODE_FULLSCREEN);
			break;

		case B_ABOUT_REQUESTED:
			be_app->AboutRequested();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
MainWindow::QuitRequested()
{
	fSettings->Save();
	be_app->PostMessage(B_QUIT_REQUESTED);

	return true;
}


void
MainWindow::SetVisible(bool visible)
{
	if (visible)
		Show();
	else
		Hide();
};


void
MainWindow::_Invoke(StationListViewItem* stationItem)
{
	if (stationItem == NULL)
		return;
	bool wasActive = false;
	if (!fAllowParallelPlayback) {
		while (!fActiveStations.IsEmpty()) {
			StationListViewItem* item = fActiveStations.LastItem();
			if (item == stationItem)
				wasActive = true;
			_TogglePlay(item);
		}
	}
	if (!wasActive)
		_TogglePlay(stationItem);
}


void
MainWindow::_TogglePlay(StationListViewItem* stationItem)
{
	switch (stationItem->State()) {
		case StreamPlayer::Stopped:
		{
			status_t status = B_ERROR;

			StreamPlayer* player = stationItem->Player();
			if (player == NULL)
				player = new StreamPlayer(stationItem->GetStation(), this);
			status = player->InitCheck();
			if (status == B_OK) {
				status = player->Play();
			} else {
				status = B_NO_MEMORY;
			}

			if (status == B_OK) {
				stationItem->StateChanged(StreamPlayer::Playing);
				fStationList->SelectItem(stationItem);
				BString success;
				success.SetToFormat(
					B_TRANSLATE("Now playing %s"), stationItem->GetStation()->Name()->String());
				fStatusBar->SetText(success);
				fStatusBar->Invalidate();
				fActiveStations.AddItem(stationItem);
			} else {
				delete player;
				player = NULL;

				BString error;
				error.SetToFormat(B_TRANSLATE("Failed playing station %s: %s"),
					stationItem->GetStation()->Name()->String(), strerror(status));
				fStatusBar->SetText(error);
			}
			stationItem->SetPlayer(player);

			break;
		}

		case StreamPlayer::Buffering:
		case StreamPlayer::Playing:
		{
			StreamPlayer* player = stationItem->Player();
			if (player != NULL)
				player->Stop();

			stationItem->StateChanged(StreamPlayer::Stopped);
			fStatusBar->SetText(NULL);
			fActiveStations.RemoveItem(stationItem);

			break;
		}

		case StreamPlayer::InActive:
		default:
			break;
	}
}


void
MainWindow::_SetSleepTimer(SleepTimerAction action, int32 minutes)
{
	delete fSleepTimerRunner;
	fSleepTimerRunner = NULL;

	bigtime_t delay;
	if (minutes > 0) {
		delay = (bigtime_t)minutes * 60 * 1000000;
	} else {
		time_t now = time(NULL);
		struct tm localTime;
		localtime_r(&now, &localTime);
		int32 seconds = (59 - localTime.tm_min) * 60 + (60 - localTime.tm_sec);
		if (seconds < 1)
			seconds = 1;
		delay = (bigtime_t)seconds * 1000000;
	}

	BMessage timerMessage(MSG_SLEEP_TIMER_FIRED);
	timerMessage.AddInt32("action", action);
	fSleepTimerRunner = new BMessageRunner(BMessenger(this), &timerMessage, delay, 1);
	if (fSleepTimerRunner->InitCheck() != B_OK) {
		delete fSleepTimerRunner;
		fSleepTimerRunner = NULL;
		fMenuCancelSleepTimer->SetEnabled(false);
		fStatusBar->SetText(B_TRANSLATE("Could not start the sleep timer."));
		return;
	}

	fMenuCancelSleepTimer->SetEnabled(true);
	const char* actionText;
	switch (action) {
		case SLEEP_TIMER_MUTE:
			actionText = B_TRANSLATE("mute playback");
			break;
		case SLEEP_TIMER_QUIT:
			actionText = B_TRANSLATE("quit StreamRadio");
			break;
		case SLEEP_TIMER_STOP:
		default:
			actionText = B_TRANSLATE("stop playback");
			break;
	}

	BString status;
	if (minutes > 0) {
		status.SetToFormat(
			B_TRANSLATE("Sleep timer will %s in %" B_PRId32 " minutes."), actionText, minutes);
	} else {
		status.SetToFormat(B_TRANSLATE("Sleep timer will %s at the top of the hour."), actionText);
	}
	fStatusBar->SetText(status.String());
}


void
MainWindow::_CancelSleepTimer()
{
	if (fSleepTimerRunner == NULL)
		return;

	delete fSleepTimerRunner;
	fSleepTimerRunner = NULL;
	fMenuCancelSleepTimer->SetEnabled(false);
	fStatusBar->SetText(B_TRANSLATE("Sleep timer cancelled."));
}


void
MainWindow::_RunSleepTimer(SleepTimerAction action)
{
	delete fSleepTimerRunner;
	fSleepTimerRunner = NULL;
	fMenuCancelSleepTimer->SetEnabled(false);

	switch (action) {
		case SLEEP_TIMER_MUTE:
			_MuteAllStreams();
			fStatusBar->SetText(B_TRANSLATE("Sleep timer muted playback."));
			break;

		case SLEEP_TIMER_QUIT:
			PostMessage(B_QUIT_REQUESTED);
			break;

		case SLEEP_TIMER_STOP:
		default:
			_StopAllStreams();
			fStatusBar->SetText(B_TRANSLATE("Sleep timer stopped playback."));
			break;
	}
}


void
MainWindow::_StopAllStreams()
{
	while (!fActiveStations.IsEmpty()) {
		StationListViewItem* stationItem = fActiveStations.LastItem();
		StreamPlayer* player = stationItem->Player();
		if (player != NULL)
			player->Stop();
		stationItem->StateChanged(StreamPlayer::Stopped);
		fActiveStations.RemoveItem(stationItem);
	}
}


void
MainWindow::_MuteAllStreams()
{
	for (int32 i = 0; i < fActiveStations.CountItems(); i++) {
		StreamPlayer* player = fActiveStations.ItemAt(i)->Player();
		if (player != NULL)
			player->SetVolume(0.0f);
	}

	int32 selectedIndex = fStationList->CurrentSelection();
	StationListViewItem* selected = fStationList->ItemAt(selectedIndex);
	if (selected != NULL && selected->Player() != NULL)
		fStationPanel->StateChanged(selected->Player()->State());
}


void
MainWindow::_SetViewMode(WindowViewMode mode)
{
	if (mode == fViewMode)
		mode = VIEW_MODE_NORMAL;

	BRect screenFrame;
	if (mode == VIEW_MODE_HEIGHT_MAX || mode == VIEW_MODE_FULLSCREEN) {
		BScreen screen(this);
		if (!screen.IsValid())
			return;
		screenFrame = screen.Frame();
	}

	if (fViewMode == VIEW_MODE_NORMAL) {
		fNormalFrame = Frame();
		fNormalLook = Look();
		fNormalStationListCollapsed = fContentSplit->IsItemCollapsed(0);
		fNormalStationPanelCollapsed = fContentSplit->IsItemCollapsed(1);
	} else {
		if (fViewMode == VIEW_MODE_FULLSCREEN)
			SetLook(fNormalLook);

		fContentSplit->SetItemCollapsed(0, fNormalStationListCollapsed);
		fContentSplit->SetItemCollapsed(1, fNormalStationPanelCollapsed);
		MoveTo(fNormalFrame.LeftTop());
		ResizeTo(fNormalFrame.Width(), fNormalFrame.Height());
	}

	fViewMode = VIEW_MODE_NORMAL;
	if (mode == VIEW_MODE_NORMAL) {
		_UpdateViewMenuItems();
		return;
	}

	switch (mode) {
		case VIEW_MODE_MINI_PLAYER:
			fContentSplit->SetItemCollapsed(0, true);
			fContentSplit->SetItemCollapsed(1, false);
			ResizeToPreferred();
			MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
			break;

		case VIEW_MODE_HEIGHT_MAX:
		{
			BRect windowFrame = Frame();
			BRect decoratorFrame = DecoratorFrame();
			float topInset = windowFrame.top - decoratorFrame.top;
			float bottomInset = decoratorFrame.bottom - windowFrame.bottom;
			MoveTo(fNormalFrame.left, screenFrame.top + topInset);
			ResizeTo(fNormalFrame.Width(), screenFrame.Height() - topInset - bottomInset);
			break;
		}

		case VIEW_MODE_FULLSCREEN:
			SetLook(B_NO_BORDER_WINDOW_LOOK);
			MoveTo(screenFrame.LeftTop());
			ResizeTo(screenFrame.Width(), screenFrame.Height());
			break;

		case VIEW_MODE_NORMAL:
		default:
			break;
	}

	fViewMode = mode;
	_UpdateViewMenuItems();
}


void
MainWindow::_UpdateViewMenuItems()
{
	fMenuMiniPlayer->SetMarked(fViewMode == VIEW_MODE_MINI_PLAYER);
	fMenuHeightMax->SetMarked(fViewMode == VIEW_MODE_HEIGHT_MAX);
	fMenuFullscreen->SetMarked(fViewMode == VIEW_MODE_FULLSCREEN);
}
