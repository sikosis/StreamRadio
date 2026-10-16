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


#include <AboutWindow.h>
#include <Catalog.h>

#include "RadioApp.h"
#include "StationFinder.h"
#include "StationFinderRadioNetwork.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RadioApp"


RadioApp::RadioApp()
	: BApplication(kAppSignature),
	  fArgvMessage(NULL)
{
}


RadioApp::~RadioApp() {}


void
RadioApp::ReadyToRun()
{
	mainWindow = new MainWindow();
	mainWindow->Show();
	if (fArgvMessage != NULL) {
		mainWindow->PostMessage(fArgvMessage);
		delete fArgvMessage;
		fArgvMessage = NULL;
	}
}


void
RadioApp::RefsReceived(BMessage* message)
{
	if (IsLaunching()) {
		if (fArgvMessage != NULL)
			delete fArgvMessage;
		fArgvMessage = new BMessage(*message);
	} else if (mainWindow != NULL) {
		mainWindow->PostMessage(message);
	}
}


void
RadioApp::ArgvReceived(int32 argc, char** argv)
{
	if (fArgvMessage == NULL)
		fArgvMessage = new BMessage(B_REFS_RECEIVED);

	int32 count = 0;
	for (int32 i = 1; i < argc; i++) {
		char* arg = argv[i];
		if (strncmp(arg, "--help", 7) == 0) {
			printf(
				B_TRANSLATE("Usage: StreamRadio <filename>\n"
							"<filename> should be a Shoutcast playlist file.\n"
							"If the station already exists, it is made to play "
							"otherwise it is added.\n"));
			exit(1);
		}

		BEntry entry(arg);
		if (entry.Exists()) {
			entry_ref entryRef;
			entry.GetRef(&entryRef);
			fArgvMessage->AddRef("refs", &entryRef);
			count++;
		}
	}

	if (count != 0) {
		if (mainWindow == NULL)
			return;
		mainWindow->PostMessage(fArgvMessage);
	}
	delete fArgvMessage;
	fArgvMessage = NULL;
}


void
RadioApp::AboutRequested()
{
	BAboutWindow* about = new BAboutWindow(B_TRANSLATE_SYSTEM_NAME("StreamRadio"), kAppSignature);

	const char* kCurrentContributors[] = {"Sikosis", NULL};
	const char* kPastContributors[] = {"Fishpond", "Humdinger", "Jacob Secunda",
		"Javier Steinaker", "Adrien Destugues", "Máximo Castañeda", "Korli",
		"Jérôme Duval", NULL};
	const char* kSpecialThanks[] = {"The Haiku Project",
		"The Community Radio Browser project", "HaikuArchives contributors and translators",
		NULL};
	const char* kLicences[] = {
		"StreamRadio — GNU General Public License, version 3 or later",
		"https://www.gnu.org/licenses/gpl-3.0.html",
		"AdapterIO compatibility code — Copyright 2016 Dario Casalinuovo "
		"and Haiku, Inc.; MIT License",
		NULL};
	const char* kDependencies[] = {
		"Haiku system C++ runtime and libraries: libbe, libtranslation, libbnetapi, "
		"libmedia, liblocalestub, libnetservices and libshared. Licence texts: "
		"/system/data/licenses",
		"https://www.haiku-os.org/",
		"Audio playback uses Haiku's Media Kit and installed media add-ons. This "
		"commonly includes an FFmpeg-based media add-on. FFmpeg is LGPL 2.1 or later, "
		"or GPL 2 or later when optional GPL components are enabled. Available codecs "
		"and licences depend on the user's Haiku installation.",
		"https://ffmpeg.org/",
		"Community Radio Browser supplies the remote station directory. Its server "
		"software is licensed under the GNU Affero General Public License, version 3.",
		"https://www.radio-browser.info/", NULL};
	const char* kSource[] = {"https://github.com/sikosis/StreamRadio", NULL};

	about->AddDescription(B_TRANSLATE("A player for online radio."));
	about->AddCopyright(2008, "The StreamRadio contributors", NULL);
	about->AddText("Current Contributor:", kCurrentContributors);
	about->AddText("Past Contributors:", kPastContributors);
	about->AddSpecialThanks(kSpecialThanks);
	about->AddText("Licences and bundled code:", kLicences);
	about->AddText("Runtime dependencies and services:", kDependencies);
	about->AddText("Source code:", kSource);

	about->Show();
}


/**
 * Application entry point
 */
int
main(int argc, char* argv[])
{
	StationFinderRadioNetwork::RegisterSelf();

	new RadioApp();
	be_app->Run();
	delete be_app;
}
