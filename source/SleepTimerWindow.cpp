/*
 * Copyright 2026 Sikosis
 * Distributed under the terms of the GNU General Public License,
 * version 3 or later.
 */


#include "SleepTimerWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SleepTimerWindow"


static const uint32 kSetTimer = 'sTGO';
static const uint32 kCloseWindow = 'sTXX';


static BMenuItem*
AddOption(BPopUpMenu* menu, const char* label, const char* field, int32 value)
{
	BMessage* message = new BMessage;
	message->AddInt32(field, value);
	BMenuItem* item = new BMenuItem(label, message);
	menu->AddItem(item);
	return item;
}


static int32
MarkedValue(BPopUpMenu* menu, const char* field, int32 fallback)
{
	BMenuItem* item = menu->FindMarked();
	if (item == NULL || item->Message() == NULL)
		return fallback;
	return item->Message()->GetInt32(field, fallback);
}


SleepTimerWindow::SleepTimerWindow(BWindow* parent)
	: BWindow(BRect(0, 0, 360, 150), B_TRANSLATE("Sleep timer"), B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	  fTarget(parent),
	  fActionMenu(new BPopUpMenu("sleep timer action")),
	  fDelayMenu(new BPopUpMenu("sleep timer delay"))
{
	AddOption(fActionMenu, B_TRANSLATE("Stop playback"), "action", SLEEP_TIMER_STOP)
		->SetMarked(true);
	AddOption(fActionMenu, B_TRANSLATE("Mute playback"), "action", SLEEP_TIMER_MUTE);
	AddOption(fActionMenu, B_TRANSLATE("Quit StreamRadio"), "action", SLEEP_TIMER_QUIT);

	AddOption(fDelayMenu, B_TRANSLATE("At top of hour"), "minutes", 0);
	AddOption(fDelayMenu, B_TRANSLATE("In 5 minutes"), "minutes", 5);
	AddOption(fDelayMenu, B_TRANSLATE("In 10 minutes"), "minutes", 10);
	AddOption(fDelayMenu, B_TRANSLATE("In 15 minutes"), "minutes", 15);
	AddOption(fDelayMenu, B_TRANSLATE("In 30 minutes"), "minutes", 30)->SetMarked(true);
	AddOption(fDelayMenu, B_TRANSLATE("In 45 minutes"), "minutes", 45);

	BMenuField* actionField = new BMenuField(
		"sleep timer action field", B_TRANSLATE("When the timer expires:"), fActionMenu);
	BMenuField* delayField
		= new BMenuField("sleep timer delay field", B_TRANSLATE("Run the timer:"), fDelayMenu);
	BButton* setButton
		= new BButton("set sleep timer", B_TRANSLATE("Set timer"), new BMessage(kSetTimer));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(actionField)
		.Add(delayField)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
		.AddGlue()
		.Add(new BButton("close sleep timer", B_TRANSLATE("Cancel"), new BMessage(kCloseWindow)))
		.Add(setButton)
		.End()
		.End();

	SetDefaultButton(setButton);
	CenterIn(parent->Frame());
}


void
SleepTimerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kSetTimer:
		{
			BMessage setTimer(MSG_SLEEP_TIMER_SET);
			setTimer.AddInt32("action", MarkedValue(fActionMenu, "action", SLEEP_TIMER_STOP));
			setTimer.AddInt32("minutes", MarkedValue(fDelayMenu, "minutes", 30));
			fTarget.SendMessage(&setTimer);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case kCloseWindow:
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
SleepTimerWindow::QuitRequested()
{
	fTarget.SendMessage(MSG_SLEEP_TIMER_WINDOW_CLOSED);
	return true;
}
