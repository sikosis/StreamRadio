/*
 * Copyright 2026 Sikosis
 * Distributed under the terms of the GNU General Public License,
 * version 3 or later.
 */


#include "AboutWindow.h"

#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <File.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Roster.h>
#include <SeparatorView.h>
#include <String.h>
#include <StringView.h>
#include <TabView.h>
#include <Url.h>
#include <View.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AboutWindow"


class IconView : public BView {
public:
	IconView(BBitmap* icon);
	virtual ~IconView();

	virtual void Draw(BRect updateRect);

private:
	BBitmap* fIcon;
};


class LinkView : public BStringView {
public:
	LinkView(const char* name, const char* label, const char* url);

	virtual void AttachedToWindow();
	virtual void MouseDown(BPoint point);

private:
	BString fUrl;
};


static BBitmap*
LoadAppIcon()
{
	app_info appInfo;
	if (be_app->GetAppInfo(&appInfo) != B_OK)
		return NULL;

	BFile appFile(&appInfo.ref, B_READ_ONLY);
	if (appFile.InitCheck() != B_OK)
		return NULL;

	BBitmap* icon = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
	if (icon->InitCheck() != B_OK) {
		delete icon;
		return NULL;
	}

	BAppFileInfo appFileInfo(&appFile);
	if (appFileInfo.GetIcon(icon, B_LARGE_ICON) != B_OK) {
		delete icon;
		return NULL;
	}

	return icon;
}


static BString
AppVersion()
{
	BString version(B_TRANSLATE("Version 1.0.1"));
	app_info appInfo;
	if (be_app->GetAppInfo(&appInfo) != B_OK)
		return version;

	BFile appFile(&appInfo.ref, B_READ_ONLY);
	BAppFileInfo appFileInfo(&appFile);
	version_info versionInfo;
	if (appFileInfo.GetVersionInfo(&versionInfo, B_APP_VERSION_KIND) == B_OK) {
		version.SetToFormat(B_TRANSLATE("Version %lu.%lu.%lu"),
			(unsigned long)versionInfo.major, (unsigned long)versionInfo.middle,
			(unsigned long)versionInfo.minor);
	}

	return version;
}


static BStringView*
Heading(const char* name, const char* text)
{
	BStringView* heading = new BStringView(name, text);
	heading->SetFont(be_bold_font);
	return heading;
}


static BView*
CreateCreditsView()
{
	BView* view = new BView("Credits", B_WILL_DRAW);
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BLayoutBuilder::Group<>(view, B_VERTICAL, 5.0f)
		.SetInsets(12.0f)
		.Add(Heading("project heading", B_TRANSLATE("Project")))
		.Add(new BStringView("copyright",
			"Copyright 2008–2026 The StreamRadio contributors"))
		.AddStrut(8.0f)
		.Add(Heading("current heading", B_TRANSLATE("Current Contributor")))
		.Add(new BStringView("current contributor", "Sikosis"))
		.AddStrut(8.0f)
		.Add(Heading("past heading", B_TRANSLATE("Past Contributors")))
		.Add(new BStringView("past contributors 1",
			"Fishpond, Humdinger, Jacob Secunda, Javier Steinaker"))
		.Add(new BStringView("past contributors 2",
			"Adrien Destugues, Máximo Castañeda, Korli, Jérôme Duval"))
		.AddStrut(8.0f)
		.Add(Heading("thanks heading", B_TRANSLATE("Special Thanks")))
		.Add(new BStringView("radio browser thanks",
			"The Community Radio Browser project"))
		.Add(new BStringView("archive thanks",
			"HaikuArchives contributors and translators"))
		.AddGlue();

	return view;
}


static BView*
CreateLicencesView()
{
	BView* view = new BView("Licences", B_WILL_DRAW);
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BLayoutBuilder::Group<>(view, B_VERTICAL, 4.0f)
		.SetInsets(12.0f)
		.Add(Heading("streamradio licence heading", "StreamRadio"))
		.Add(new BStringView("streamradio licence",
			"GNU General Public License, version 3 or later"))
		.Add(new LinkView("gpl link", "GNU GPL version 3",
			"https://www.gnu.org/licenses/gpl-3.0.html"))
		.AddStrut(5.0f)
		.Add(Heading("adapter licence heading", "AdapterIO compatibility code"))
		.Add(new BStringView("adapter copyright",
			"Copyright 2016 Dario Casalinuovo and Haiku, Inc.; MIT License"))
		.Add(new LinkView("mit link", "MIT License",
			"https://opensource.org/license/mit"))
		.AddStrut(5.0f)
		.Add(Heading("ffmpeg dependency heading", "Media codecs"))
		.Add(new BStringView("ffmpeg dependency",
			"Playback uses installed Haiku media add-ons, commonly FFmpeg-based."))
		.Add(new BStringView("ffmpeg licence",
			"FFmpeg is LGPL 2.1+ or GPL 2+ when optional GPL code is enabled."))
		.Add(new LinkView("ffmpeg link", "FFmpeg", "https://ffmpeg.org/"))
		.AddStrut(5.0f)
		.Add(Heading("radio browser heading", "Community Radio Browser"))
		.Add(new BStringView("radio browser licence",
			"Remote station directory service; server software is GNU AGPL v3."))
		.Add(new LinkView("radio browser link", "Radio Browser",
			"https://www.radio-browser.info/"))
		.AddGlue();

	return view;
}


IconView::IconView(BBitmap* icon)
	: BView("application icon", B_WILL_DRAW),
	  fIcon(icon)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetExplicitMinSize(BSize(96, 96));
	SetExplicitMaxSize(BSize(96, 96));
}


IconView::~IconView()
{
	delete fIcon;
}


void
IconView::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fIcon == NULL)
		return;

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmap(fIcon, fIcon->Bounds(), Bounds(), B_FILTER_BITMAP_BILINEAR);
}


LinkView::LinkView(const char* name, const char* label, const char* url)
	: BStringView(name, label),
	  fUrl(url)
{
	SetToolTip(url);
	BFont font;
	GetFont(&font);
	font.SetFace(font.Face() | B_UNDERSCORE_FACE);
	SetFont(&font);
}


void
LinkView::AttachedToWindow()
{
	BStringView::AttachedToWindow();
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	int32 luminance
		= (299 * background.red + 587 * background.green + 114 * background.blue) / 1000;
	if (luminance < 128)
		SetHighColor(105, 190, 255);
	else
		SetHighColor(0, 76, 168);
}


void
LinkView::MouseDown(BPoint point)
{
	(void)point;
	BUrl url(fUrl.String(), true);
	url.OpenWithPreferredApplication(true);
}


AboutWindow::AboutWindow()
	: BWindow(BRect(0, 0, 640, 620), B_TRANSLATE("About StreamRadio"), B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE)
{
	BStringView* title = new BStringView("title", B_TRANSLATE_SYSTEM_NAME("StreamRadio"));
	BFont titleFont(*be_bold_font);
	titleFont.SetSize(be_plain_font->Size() * 2.0f);
	title->SetFont(&titleFont);

	BString versionText = AppVersion();
	BStringView* version = new BStringView("version", versionText.String());
	BStringView* description
		= new BStringView("description", B_TRANSLATE("A player for online radio."));

	BTabView* tabs = new BTabView("about tabs", B_WIDTH_FROM_LABEL);
	tabs->AddTab(CreateCreditsView());
	tabs->AddTab(CreateLicencesView());

	BButton* closeButton
		= new BButton("close", B_TRANSLATE("Close"), new BMessage(B_QUIT_REQUESTED));
	closeButton->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 10.0f)
		.SetInsets(18.0f)
		.AddGroup(B_HORIZONTAL, 16.0f)
			.Add(new IconView(LoadAppIcon()))
			.AddGroup(B_VERTICAL, 4.0f)
				.Add(title)
				.Add(version)
				.AddStrut(4.0f)
				.Add(description)
				.Add(new LinkView("source link", "github.com/sikosis/StreamRadio",
					"https://github.com/sikosis/StreamRadio"))
				.AddGlue()
			.End()
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(tabs, 1.0f)
		.AddGroup(B_HORIZONTAL, 0.0f)
			.AddGlue()
			.Add(closeButton)
		.End();

	CenterOnScreen();
}
