/*
 * Copyright 2026 Sikosis
 * Distributed under the terms of the GNU General Public License,
 * version 3 or later.
 */
#ifndef _STREAMRADIO_SLEEP_TIMER_WINDOW_H
#define _STREAMRADIO_SLEEP_TIMER_WINDOW_H


#include <Messenger.h>
#include <Window.h>


#define MSG_SLEEP_TIMER_OPEN 'sTOP'
#define MSG_SLEEP_TIMER_SET 'sTST'
#define MSG_SLEEP_TIMER_CANCEL 'sTCN'
#define MSG_SLEEP_TIMER_FIRED 'sTFR'
#define MSG_SLEEP_TIMER_WINDOW_CLOSED 'sTCL'


enum SleepTimerAction {
	SLEEP_TIMER_STOP = 0,
	SLEEP_TIMER_MUTE,
	SLEEP_TIMER_QUIT
};


class BPopUpMenu;


class SleepTimerWindow : public BWindow {
public:
	SleepTimerWindow(BWindow* parent);

	void ActivateWindow();
	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

private:
	BMessenger fTarget;
	BPopUpMenu* fActionMenu;
	BPopUpMenu* fDelayMenu;
};


#endif	// _STREAMRADIO_SLEEP_TIMER_WINDOW_H
