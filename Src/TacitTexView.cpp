// TacitTexView.cpp
//
// A texture viewer for various formats.
//
// Copyright (c) 2018, 2019, 2020 Tristan Grimmer.
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
// granted, provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
// AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include <dwmapi.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>				// Include glfw3.h after our OpenGL definitions.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <Foundation/tVersion.h>
#include <System/tCommand.h>
#include <Image/tPicture.h>
#include <System/tFile.h>
#include <System/tTime.h>
#include <System/tScript.h>
#include <Math/tHash.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "TacitTexView.h"
#include "TacitImage.h"
#include "Dialogs.h"
#include "ContactSheet.h"
#include "ContentView.h"
#include "ImGuiLogWindow.h"
#include "Settings.h"
using namespace tStd;
using namespace tSystem;


namespace TexView
{
	tCommand::tParam ImageFileParam(1, "ImageFile", "File to open.");
	int MajorVersion							= 1;
	int MinorVersion							= 0;
	int Revision								= 5;
	Settings Config;
	bool LogWindowOpen							= true;
	TexView::ImGuiLog LogWindow;
	tList<TacitImage> Images;
	tItList<TacitImage> ImagesTimeSorted(false);
	tuint256 ImagesHash							= 0;
	TacitImage* CurrImage						= nullptr;
	TacitImage CursorImage;
	TacitImage PrevImage;
	TacitImage NextImage;
	TacitImage FlipHImage;
	TacitImage FlipVImage;
	TacitImage RotateACWImage;
	TacitImage RotateCWImage;
	TacitImage FullscreenImage;
	TacitImage WindowedImage;
	TacitImage SkipBeginImage;
	TacitImage SkipEndImage;
	TacitImage MipmapsImage;
	TacitImage CubemapImage;
	TacitImage RecycleImage;
	TacitImage InfoOverlayImage;
	TacitImage TileImage;
	TacitImage StopImage;
	TacitImage PlayImage;
	TacitImage ContentViewImage;
	TacitImage DefaultThumbnailImage;

	GLFWwindow* Window							= nullptr;
	double DisappearCountdown					= DisappearDuration;
	double SlideshowCountdown					= 0.0;
	bool SlideshowPlaying						= false;
	bool FullscreenMode							= false;
	bool WindowIconified						= false;
	bool ShowCheatSheet							= false;
	bool ShowAbout								= false;
	bool SaveAsDialog							= false;
	bool JustOpenedSaveAsDialog					= false;
	bool ContactDialog							= false;
	bool JustOpenedContactDialog				= false;
	bool PrefsDialog							= false;
	bool DoDeleteFile							= false;
	bool RMBDown								= false;
	int DragAnchorX								= 0;
	int DragAnchorY								= 0;

	enum class ZoomMode
	{
		User,
		Fit,
		Downscale,
		OneToOne
	};
	ZoomMode CurrZoomMode						= ZoomMode::Downscale;

	float ZoomPercent							= 100.0f;

	int Dispw									= 1;
	int Disph									= 1;
	int PanOffsetX								= 0;
	int PanOffsetY								= 0;
	int DragDownOffsetX							= 0;
	int DragDownOffsetY							= 0;
	int CursorX									= -1;
	int CursorY									= -1;
	tColouri PixelColour						= tColouri::black;

	const ImVec4 ColourEnabledTint				= ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	const ImVec4 ColourDisabledTint				= ImVec4(0.46f, 0.46f, 0.58f, 1.00f);
	const ImVec4 ColourBG						= ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	const ImVec4 ColourPressedBG				= ImVec4(0.45f, 0.45f, 0.60f, 1.00f);

	const float ZoomMin							= 20.0f;
	const float ZoomMax							= 2500.0f;

	void DrawBackground(float bgX, float bgY, float bgW, float bgH);
	void DrawTextureViewerLog(float x, float y, float w, float h);
	void PrintRedirectCallback(const char* text, int numChars)															{ LogWindow.AddLog("%s", text); }
	void GlfwErrorCallback(int error, const char* description)															{ tPrintf("Glfw Error %d: %s\n", error, description); }
	bool CompareFunc(const tStringItem& a, const tStringItem& b)														{ return tStricmp(a.ConstText(), b.ConstText()) < 0; }

	void LoadCurrImage();
	void SetWindowTitle();
	bool OnPrevious(bool circ = false);
	bool OnNext(bool circ = false);
	bool OnSkipBegin();
	bool OnSkipEnd();
	void ShowDeleteFileModal();
	bool DeleteImageFile(const tString& imgFile);
	void ResetPan(bool resetX = true, bool resetY = true);
	void ApplyZoomDelta(float zoomDelta, float roundTo, bool correctPan);
	void FindImageFiles(tList<tStringItem>& foundFiles);
	tuint256 ComputeImagesHash(const tList<tStringItem>& files);
	void Update(GLFWwindow* window, double dt, bool dopoll = true);

	void WindowRefreshFun(GLFWwindow* window)																			{ Update(window, 0.0, false); }
	void KeyCallback(GLFWwindow*, int key, int scancode, int action, int modifiers);
	void MouseButtonCallback(GLFWwindow*, int mouseButton, int x, int y);
	void CursorPosCallback(GLFWwindow*, double x, double y);
	void ScrollWheelCallback(GLFWwindow*, double x, double y);
	void FileDropCallback(GLFWwindow*, int count, const char** paths);
	void FocusCallback(GLFWwindow*, int gotFocus);
	void IconifyCallback(GLFWwindow*, int iconified);
}


void TexView::DrawTextureViewerLog(float x, float y, float w, float h)
{
	// We take advantage of the fact that multiple calls to Begin()/End() are appending to the same window.
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);

	ImGui::Begin("InfoLog", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
	LogWindow.Draw("Log", &LogWindowOpen);
	ImGui::End();
}


void TexView::FindImageFiles(tList<tStringItem>& foundFiles)
{
	tString imagesDir = tSystem::tGetCurrentDir();
	if (ImageFileParam.IsPresent() && tSystem::tIsAbsolutePath(ImageFileParam.Get()))
		imagesDir = tSystem::tGetDir(ImageFileParam.Get());

	tPrintf("Looking for image files in %s\n", imagesDir.ConstText());
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.jpg");
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.gif");
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.tga");
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.png");
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.tif");
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.tiff");
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.bmp");
	tSystem::tFindFilesInDir(foundFiles, imagesDir, "*.dds");
	foundFiles.Sort(CompareFunc, tListSortAlgorithm::Merge);
}


tuint256 TexView::ComputeImagesHash(const tList<tStringItem>& files)
{
	tuint256 hash = 0;
	for (tStringItem* item = files.First(); item; item = item->Next())
		hash = tMath::tHashString256(item->ConstText(), hash);

	return hash;
}


void TexView::PopulateImages()
{
	Images.Clear();
	ImagesTimeSorted.Clear();

	tList<tStringItem> foundFiles;
	FindImageFiles(foundFiles);
	ImagesHash = ComputeImagesHash(foundFiles);

	for (tStringItem* filename = foundFiles.First(); filename; filename = filename->Next())
	{
		TacitImage* newImg = new TacitImage(*filename);
		Images.Append(newImg);
		ImagesTimeSorted.Append(newImg);
	}

	CurrImage = nullptr;
}


void TexView::SetCurrentImage(const tString& currFilename)
{
	for (TacitImage* si = Images.First(); si; si = si->Next())
	{
		tString siName = tSystem::tGetFileName(si->Filename);
		tString imgName = tSystem::tGetFileName(currFilename);

		if (tStricmp(siName.ConstText(), imgName.ConstText()) == 0)
		{
			CurrImage = si;
			break;
		}
	}

	if (!CurrImage)
	{
		CurrImage = Images.First();
		if (!currFilename.IsEmpty())
			tPrintf("Could not display [%s].\n", tSystem::tGetFileName(currFilename).ConstText());
		if (CurrImage && !CurrImage->Filename.IsEmpty())
			tPrintf("Displaying [%s] instead.\n", tSystem::tGetFileName(CurrImage->Filename).ConstText());
	}

	if (CurrImage)
	{
		CurrZoomMode = ZoomMode::Downscale;
		LoadCurrImage();
	}
}


bool LessThanImageLoadTime(const TacitImage& a, const TacitImage& b)
{
	return a.GetLoadedTime() < b.GetLoadedTime();
}


void TexView::LoadCurrImage()
{
	tAssert(CurrImage);
	bool imgJustLoaded = false;
	if (!CurrImage->IsLoaded())
		imgJustLoaded = CurrImage->Load();

	if (!SlideshowPlaying)
		CurrImage->PrintInfo();
	SetWindowTitle();
	ResetPan();

	// We only need to consider unloading an image when a new one is loaded... in this function.
	// We currently do not allow unloading when in slideshow and the frame duration is small.
	bool slideshowSmallDuration = SlideshowPlaying && (Config.SlidehowFrameDuration < 0.5f);
	if (imgJustLoaded && !slideshowSmallDuration)
	{
		ImagesTimeSorted.Sort(LessThanImageLoadTime);

		int64 usedMem = 0;
		for (tItList<TacitImage>::Iter iter = ImagesTimeSorted.First(); iter; iter++)
			usedMem += int64((*iter).Info.MemSizeBytes);

		int64 allowedMem = int64(Config.MaxImageMemMB) * 1024 * 1024;
		if (usedMem > allowedMem)
		{
			tPrintf("Used image mem (%|64d) bigger than max (%|64d). Unloading.\n", usedMem, allowedMem);
			for (tItList<TacitImage>::Iter iter = ImagesTimeSorted.First(); iter; iter++)
			{
				TacitImage* i = iter.GetObject();

				// Never unload the current image.
				if (i->IsLoaded() && (i != CurrImage))
				{
					tPrintf("Unloading %s freeing %d Bytes\n", tSystem::tGetFileName(i->Filename).ConstText(), i->Info.MemSizeBytes);
					usedMem -= i->Info.MemSizeBytes;
					i->Unload();
					if (usedMem < allowedMem)
						break;
				}
			}
			tPrintf("Used mem %|64dB out of max %|64dB.\n", usedMem, allowedMem);
		}
	}
}


bool TexView::OnPrevious(bool circ)
{
	if (!CurrImage || (!circ && !CurrImage->Prev()))
		return false;

	CurrImage = circ ? Images.PrevCirc(CurrImage) : CurrImage->Prev();
	LoadCurrImage();
	return true;
}


bool TexView::OnNext(bool circ)
{
	if (!CurrImage || (!circ && !CurrImage->Next()))
		return false;

	CurrImage = circ ? Images.NextCirc(CurrImage) : CurrImage->Next();
	LoadCurrImage();
	return true;
}


bool TexView::OnSkipBegin()
{
	if (!CurrImage || !Images.First())
		return false;

	CurrImage = Images.First();
	LoadCurrImage();
	return true;
}


bool TexView::OnSkipEnd()
{
	if (!CurrImage || !Images.Last())
		return false;

	CurrImage = Images.Last();
	LoadCurrImage();
	return true;
}


void TexView::ShowHelpMark(const char* desc)
{
	ImGui::TextDisabled("[?]");
	if (!ImGui::IsItemHovered())
		return;

	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
	ImGui::TextUnformatted(desc);
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}


void TexView::ShowToolTip(const char* desc)
{
	if (!ImGui::IsItemHovered())
		return;

	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
	ImGui::TextUnformatted(desc);
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}


void TexView::SetWindowTitle()
{
	if (!Window)
		return;

	tString title = "Tacit Viewer";
	if (CurrImage && !CurrImage->Filename.IsEmpty())
		title = title + " - " + tGetFileName(CurrImage->Filename);

	glfwSetWindowTitle(Window, title.ConstText());
}


void TexView::ResetPan(bool resetX, bool resetY)
{
	if (resetX)
	{
		PanOffsetX = 0;
		DragDownOffsetX = 0;
	}

	if (resetY)
	{
		PanOffsetY = 0;
		DragDownOffsetY = 0;
	}
}


void TexView::DrawBackground(float bgX, float bgY, float bgW, float bgH)
{
	switch (Config.BackgroundStyle)
	{
		case Settings::BGStyle::None:
			return;

		case Settings::BGStyle::Checkerboard:
		{
			// Semitransparent checkerboard background.
			int x = 0;
			int y = 0;
			bool lineStartToggle = false;
			float checkSize = 16.0f;
			while (y*checkSize < bgH)
			{
				bool colourToggle = lineStartToggle;

				while (x*checkSize < bgW)
				{
					if (colourToggle)
						glColor4f(0.3f, 0.3f, 0.35f, 1.0f);
					else
						glColor4f(0.4f, 0.4f, 0.45f, 1.0f);

					colourToggle = !colourToggle;

					float cw = checkSize;
					if ((x+1)*checkSize > bgW)
						cw -= (x+1)*checkSize - bgW;

					float ch = checkSize;
					if ((y+1)*checkSize > bgH)
						ch -= (y+1)*checkSize - bgH;

					float l = tMath::tRound(bgX+x*checkSize);
					float r = tMath::tRound(bgX+x*checkSize+cw);
					float b = tMath::tRound(bgY+y*checkSize);
					float t = tMath::tRound(bgY+y*checkSize+ch);

					glBegin(GL_QUADS);
					glVertex2f(l, b);
					glVertex2f(l, t);
					glVertex2f(r, t);
					glVertex2f(r, b);
					glEnd();

					x++;
				}
				x = 0;
				y++;
				lineStartToggle = !lineStartToggle;
			}
			break;
		}

		case Settings::BGStyle::Black:
		case Settings::BGStyle::Grey:
		case Settings::BGStyle::White:
		{
			switch (Config.BackgroundStyle)
			{
				case Settings::BGStyle::Black:	glColor4f(0.0f, 0.0f, 0.0f, 1.0f);		break;
				case Settings::BGStyle::Grey:	glColor4f(0.25f, 0.25f, 0.3f, 1.0f);	break;
				case Settings::BGStyle::White:	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);		break;
			}
			float l = tMath::tRound(bgX);
			float r = tMath::tRound(bgX+bgW);
			float b = tMath::tRound(bgY);
			float t = tMath::tRound(bgY+bgH);

			glBegin(GL_QUADS);
			glVertex2f(l, b);
			glVertex2f(l, t);
			glVertex2f(r, t);
			glVertex2f(r, b);
			glEnd();

			break;
		}
	}
}


void TexView::Update(GLFWwindow* window, double dt, bool dopoll)
{
	// Poll and handle events like inputs, window resize, etc. You can read the io.WantCaptureMouse,
	// io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
	//
	// When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
	// When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
	//
	// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those
	// two flags.
	if (dopoll)
		glfwPollEvents();

	ImVec4 clearColour = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
	glClearColor(clearColour.x, clearColour.y, clearColour.z, clearColour.w);
	glClear(GL_COLOR_BUFFER_BIT);
	int bottomUIHeight = 150;
	int topUIHeight = 26;
	if (FullscreenMode)
	{
		bottomUIHeight = 0;
		topUIHeight = 0;
	}
	else if (!Config.ShowLog)
	{
		bottomUIHeight = 0;
	}

	ImGui_ImplOpenGL2_NewFrame();		
	ImGui_ImplGlfw_NewFrame();
	int dispw, disph;
	glfwGetFramebufferSize(window, &dispw, &disph);
	if ((dispw != Dispw) || (disph != Disph))
	{
		Dispw = dispw;
		Disph = disph;
		if ((PanOffsetX+DragDownOffsetX == 0) && (PanOffsetY+DragDownOffsetY == 0))
			ResetPan();
	}

	int workAreaW = Dispw;
	int workAreaH = Disph - bottomUIHeight - topUIHeight;
	float workAreaAspect = float(workAreaW)/float(workAreaH);

	glViewport(0, bottomUIHeight, workAreaW, workAreaH);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, workAreaW, 0, workAreaH, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	float draww = 1.0f;
	float drawh = 1.0f;
	float iw = 1.0f;
	float ih = 1.0f;
	float hmargin = 0.0f;
	float vmargin = 0.0f;
	static int imgxi = 0;
	static int imgyi = 0;

	if (CurrImage)
	{
		iw = float(CurrImage->GetWidth());
		ih = float(CurrImage->GetHeight());
		float picAspect = iw/ih;

		if (workAreaAspect > picAspect)
		{
			drawh = float(workAreaH);
			draww = picAspect * drawh;
			hmargin = (workAreaW - draww) * 0.5f;
			vmargin = 0.0f;;
		}
		else
		{
			draww = float(workAreaW);
			drawh = draww / picAspect;
			vmargin = (workAreaH - drawh) * 0.5f;
			hmargin = 0.0f;
		}

		// w and h are the image width and height. draww and drawh are the drawable area width and height.
		float l = tMath::tRound(hmargin);
		float r = tMath::tRound(hmargin+draww);
		float b = tMath::tRound(vmargin);
		float t = tMath::tRound(vmargin+drawh);

		float uvUMarg = 0.0f;
		float uvVMarg = 0.0f;

		if (CurrZoomMode == ZoomMode::Downscale)
		{
			ZoomPercent = 100.0f;
			if (draww < iw)
				ZoomPercent = 100.0f * draww / iw;
		}
		else if (CurrZoomMode == ZoomMode::Fit)
		{
			ZoomPercent = 100.0f * draww / iw;
		}

		float w = iw * ZoomPercent/100.0f;
		float h = ih * ZoomPercent/100.0f;

		// If the image is smaller than the drawable area we draw a quad of the correct size with full 0..1 range in the uvs.
		if (w < draww)
		{
			float offsetW = tMath::tRound((draww - w) / 2.0f);
			l += offsetW;
			r -= offsetW;
			float offsetH = tMath::tRound((drawh - h) / 2.0f);
			b += offsetH;
			t -= offsetH;
		}
		else
		{
			float propw = draww / w;
			uvUMarg = (1.0f - propw)/2.0f;
			float proph = drawh / h;
			uvVMarg = (1.0f - proph)/2.0f;
		}

		// Modify the UVs here to magnify.
		if ((draww < w) || Config.Tile)
		{
			if (RMBDown)
			{
				double xpos, ypos;
				glfwGetCursorPos(window, &xpos, &ypos);
				DragDownOffsetX = int(xpos) - DragAnchorX;
			}

			if (!Config.Tile)
				tMath::tClamp(DragDownOffsetX, int(-(w-draww)/2.0f) - PanOffsetX, int((w-draww)/2.0f) - PanOffsetX);
		}

		if ((drawh < h) || Config.Tile)
		{
			if (RMBDown)
			{
				double xpos, ypos;
				glfwGetCursorPos(window, &xpos, &ypos);
				DragDownOffsetY = int(ypos) - DragAnchorY;
			}

			if (!Config.Tile)
				tMath::tClamp(DragDownOffsetY, int(-(h-drawh)/2.0f) - PanOffsetY, int((h-drawh)/2.0f) - PanOffsetY);
		}

		if ((draww > w) && !Config.Tile)
			ResetPan(true, false);

		if ((drawh > h) && !Config.Tile)
			ResetPan(false, true);

		float uvUOff = -float(PanOffsetX+DragDownOffsetX)/w;
		float uvVOff =  float(PanOffsetY+DragDownOffsetY)/h;

		// Draw background.
		if (Config.BackgroundExtend || Config.Tile)
			DrawBackground(hmargin, vmargin, draww, drawh);
		else
			DrawBackground(l, b, r-l, t-b);

		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		CurrImage->Bind();
		glEnable(GL_TEXTURE_2D);

		glBegin(GL_QUADS);
		if (!Config.Tile)
		{
			glTexCoord2f(0.0f + uvUMarg + uvUOff, 0.0f + uvVMarg + uvVOff); glVertex2f(l, b);
			glTexCoord2f(0.0f + uvUMarg + uvUOff, 1.0f - uvVMarg + uvVOff); glVertex2f(l, t);
			glTexCoord2f(1.0f - uvUMarg + uvUOff, 1.0f - uvVMarg + uvVOff); glVertex2f(r, t);
			glTexCoord2f(1.0f - uvUMarg + uvUOff, 0.0f + uvVMarg + uvVOff); glVertex2f(r, b);
		}
		else
		{
			float repU = draww/(r-l);	float offU = (1.0f-repU)/2.0f;
			float repV = drawh/(t-b);	float offV = (1.0f-repV)/2.0f;
			glTexCoord2f(offU + 0.0f + uvUMarg + uvUOff,	offV + 0.0f + uvVMarg + uvVOff);	glVertex2f(hmargin,			vmargin);
			glTexCoord2f(offU + 0.0f + uvUMarg + uvUOff,	offV + repV - uvVMarg + uvVOff);	glVertex2f(hmargin,			vmargin+drawh);
			glTexCoord2f(offU + repU - uvUMarg + uvUOff,	offV + repV - uvVMarg + uvVOff);	glVertex2f(hmargin+draww,	vmargin+drawh);
			glTexCoord2f(offU + repU - uvUMarg + uvUOff,	offV + 0.0f + uvVMarg + uvVOff);	glVertex2f(hmargin+draww,	vmargin);
		}
		glEnd();

		// Show cursor colour inspector crosshairs.
		float cw = float((CursorImage.GetWidth() - 1) >> 1);
		float ch = float((CursorImage.GetHeight() - 1) >> 1);
		float cx = float(CursorX);
		float cy = float(CursorY);
		float areaH = float(workAreaH) + topUIHeight;

		float picX = cx - l;
		float picY = (areaH - cy - 1) - b;
		float normX = picX / (r-l);
		float normY = picY / (t-b);
		if (Config.Tile)
		{
			normX = tMath::tMod(normX, 1.0f);
			if (normX < 0.0f) normX += 1.0f;

			normY = tMath::tMod(normY, 1.0f);
			if (normY < 0.0f) normY += 1.0f;
		}
		float imgx = iw * tMath::tLesc(normX, 0.0f + uvUMarg + uvUOff, 1.0f - uvUMarg + uvUOff);
		float imgy = ih * tMath::tLesc(normY, 0.0f + uvVMarg + uvVOff, 1.0f - uvVMarg + uvVOff);

		imgxi = int(imgx);
		imgyi = int(imgy);

		if (!Config.Tile)
		{
			tMath::tClamp(imgxi, 0, CurrImage->GetWidth() - 1);
			tMath::tClamp(imgyi, 0, CurrImage->GetHeight() - 1);
		}
		else
		{
			imgxi %= CurrImage->GetWidth();
			if (imgxi < 0) imgxi += CurrImage->GetWidth();
			imgyi %= CurrImage->GetHeight();
			if (imgyi < 0) imgyi += CurrImage->GetHeight();
		}
		PixelColour = CurrImage->GetPixel(imgxi, imgyi);

		if ((DisappearCountdown > 0.0) || Config.InfoOverlayShow)
		{
			CursorImage.Bind();
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(cx-cw, areaH-(cy+ch));
			glTexCoord2f(0.0f, 1.0f); glVertex2f(cx-cw, areaH-(cy-ch));
			glTexCoord2f(1.0f, 1.0f); glVertex2f(cx+cw, areaH-(cy-ch));
			glTexCoord2f(1.0f, 0.0f); glVertex2f(cx+cw, areaH-(cy+ch));
			glEnd();
		}

		glDisable(GL_TEXTURE_2D);
		glFlush(); // Don't need this with GLUT_DOUBLE and glutSwapBuffers.
	}

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, dispw, 0, disph, -1, 1);
	glMatrixMode(GL_MODELVIEW);

	ImGui::NewFrame();
	
	// Show the big demo window. You can browse its code to learn more about Dear ImGui.
	static bool showDemoWindow = false;
	//static bool showDemoWindow = true;
	if (showDemoWindow)
		ImGui::ShowDemoWindow(&showDemoWindow);

	ImGuiWindowFlags flagsImgButton =
		ImGuiWindowFlags_NoTitleBar		|	ImGuiWindowFlags_NoScrollbar	|	ImGuiWindowFlags_NoMove			| ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse		|	ImGuiWindowFlags_NoNav			|	ImGuiWindowFlags_NoBackground	| ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
		DisappearCountdown -= dt;

	if (DisappearCountdown > 0.0)
	{
		if ((CurrImage != Images.First()) || SlideshowPlaying)
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, float(topUIHeight) + float(workAreaH)*0.5f - 33.0f));
			ImGui::SetNextWindowSize(ImVec2(16, 70), ImGuiCond_Always);
			ImGui::Begin("Prev", nullptr, flagsImgButton);
			ImGui::SetCursorPos(ImVec2(6, 2));
			if (ImGui::ImageButton(ImTextureID(PrevImage.Bind()), ImVec2(15,56), ImVec2(0,0), ImVec2(1,1), 3, ImVec4(0,0,0,0), ImVec4(1,1,1,1)))
				OnPrevious();
			ImGui::End();
		}

		if ((CurrImage != Images.Last()) || SlideshowPlaying)
		{
			ImGui::SetNextWindowPos(ImVec2(workAreaW - 33.0f, float(topUIHeight) + float(workAreaH) * 0.5f - 33.0f));
			ImGui::SetNextWindowSize(ImVec2(16, 70), ImGuiCond_Always);
			ImGui::Begin("Next", nullptr, flagsImgButton);
			ImGui::SetCursorPos(ImVec2(6, 2));
			if (ImGui::ImageButton(ImTextureID(NextImage.Bind()), ImVec2(15,56), ImVec2(0,0), ImVec2(1,1), 3, ImVec4(0,0,0,0), ImVec4(1,1,1,1)))
				OnNext();
			ImGui::End();
		}

		// Skip to beginning button.
		if ((CurrImage != Images.First()) || SlideshowPlaying)
		{
			ImGui::SetNextWindowPos(ImVec2((workAreaW>>1)-22.0f-60.0f, float(topUIHeight) + float(workAreaH) - 42.0f));
			ImGui::SetNextWindowSize(ImVec2(40, 40), ImGuiCond_Always);
			ImGui::Begin("SkipBegin", nullptr, flagsImgButton);
			if (ImGui::ImageButton(ImTextureID(SkipBeginImage.Bind()), ImVec2(24,24), ImVec2(0,0), ImVec2(1,1), 2, ImVec4(0,0,0,0), ImVec4(1,1,1,1)))
				OnSkipBegin();
			ImGui::End();
		}

		// Slideshow/Stop button.
		ImGui::SetNextWindowPos(ImVec2((workAreaW>>1)-22.0f-20.0f, float(topUIHeight) + float(workAreaH) - 42.0f));
		ImGui::SetNextWindowSize(ImVec2(40, 40), ImGuiCond_Always);
		ImGui::Begin("Slideshow", nullptr, flagsImgButton);
		uint64 psImageID = SlideshowPlaying ? StopImage.Bind() : PlayImage.Bind();
		if (ImGui::ImageButton(ImTextureID(psImageID), ImVec2(24,24), ImVec2(0,0), ImVec2(1,1), 2, ImVec4(0,0,0,0), ImVec4(1,1,1,1)))
			SlideshowPlaying = !SlideshowPlaying;
		ImGui::End();

		// Fullscreen / Windowed button.
		ImGui::SetNextWindowPos(ImVec2((workAreaW>>1)-22.0f+20.0f, float(topUIHeight) + float(workAreaH) - 42.0f));
		ImGui::SetNextWindowSize(ImVec2(40, 40), ImGuiCond_Always);
		ImGui::Begin("Fullscreen", nullptr, flagsImgButton);
		uint64 fsImageID = FullscreenMode ? WindowedImage.Bind() : FullscreenImage.Bind();
		if (ImGui::ImageButton(ImTextureID(fsImageID), ImVec2(24,24), ImVec2(0,0), ImVec2(1,1), 2, ImVec4(0,0,0,0), ImVec4(1,1,1,1)))
			ChangeScreenMode(!FullscreenMode);
		ImGui::End();

		// Skip to end button.
		if ((CurrImage != Images.Last()) || SlideshowPlaying)
		{
			ImGui::SetNextWindowPos(ImVec2((workAreaW>>1)-22.0f+60.0f, float(topUIHeight) + float(workAreaH) - 42.0f));
			ImGui::SetNextWindowSize(ImVec2(40, 40), ImGuiCond_Always);
			ImGui::Begin("SkipEnd", nullptr, flagsImgButton);
			if (ImGui::ImageButton(ImTextureID(SkipEndImage.Bind()), ImVec2(24,24), ImVec2(0,0), ImVec2(1,1), 2, ImVec4(0,0,0,0), ImVec4(1,1,1,1)))
				OnSkipEnd();
			ImGui::End();
		}
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0));

	if (!FullscreenMode)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,6));
		ImGui::BeginMainMenuBar();
		{
			static bool saveAllAsDialog = false;
			if (ImGui::BeginMenu("File"))
			{
				// Show file menu items...
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,3));

				if (ImGui::MenuItem("Save As...", "Ctrl-S") && CurrImage)
				{
					JustOpenedSaveAsDialog = !SaveAsDialog;
					SaveAsDialog = !SaveAsDialog;
				}

				if (ImGui::MenuItem("Save All As...") && CurrImage)
					saveAllAsDialog = !saveAllAsDialog;

				ImGui::Separator();
				if (ImGui::MenuItem("Quit", "Alt-F4"))
					glfwSetWindowShouldClose(Window, 1);

				ImGui::PopStyleVar();
				ImGui::EndMenu();
			}
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,3));
			if (SaveAsDialog)
			{
				ShowSaveAsDialog(&SaveAsDialog, JustOpenedSaveAsDialog);
				JustOpenedSaveAsDialog = false;
			}
			if (saveAllAsDialog)
				ShowSaveAllAsDialog(&saveAllAsDialog);
			ImGui::PopStyleVar();

			if (ImGui::BeginMenu("Edit"))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,3));
				if (ImGui::MenuItem("Contact Sheet...", "C") && (Images.GetNumItems() > 1))
				{
					JustOpenedContactDialog = !ContactDialog;
					ContactDialog = !ContactDialog;
				}
				
				if (ImGui::MenuItem("Preferences...", "P"))
					PrefsDialog = !PrefsDialog;

				ImGui::PopStyleVar();
				ImGui::EndMenu();
			}
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,3));
			if (ContactDialog)
			{
				ShowContactSheetDialog(&ContactDialog, JustOpenedContactDialog);
				JustOpenedContactDialog = false;
			}

			if (PrefsDialog)
				ShowPreferencesDialog(&PrefsDialog);

			ImGui::PopStyleVar();

			if (ImGui::BeginMenu("View"))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,3));
				ImGui::MenuItem("Log", "L", &Config.ShowLog, true);
				ImGui::MenuItem("Info Overlay", "I", &Config.InfoOverlayShow, true);
				ImGui::MenuItem("Content View", "V", &Config.ContentViewShow, true);

				ImGui::Separator();

				bool userMode = CurrZoomMode == ZoomMode::User;
				if (ImGui::MenuItem("Zoom User", "", &userMode, true))
				{
					ResetPan();
					CurrZoomMode = ZoomMode::User;
				}

				bool fitMode = CurrZoomMode == ZoomMode::Fit;
				if (ImGui::MenuItem("Zoom Fit", "F", &fitMode, true))
				{
					ResetPan();
					CurrZoomMode = ZoomMode::Fit;
				}

				bool downscale = CurrZoomMode == ZoomMode::Downscale;
				if (ImGui::MenuItem("Zoom Downscale", "D", &downscale, true))
				{
					ResetPan();
					CurrZoomMode = ZoomMode::Downscale;
				}

				bool oneToOne = CurrZoomMode == ZoomMode::OneToOne;
				if (ImGui::MenuItem("Zoom 1:1", "Z", &oneToOne, true))
				{
					ZoomPercent = 100.0f;
					ResetPan();
					CurrZoomMode = ZoomMode::OneToOne;
				}

				ImGui::PushItemWidth(60);
				const char* zoomItems[] = { "Zoom", "20%", "50%", "100%", "150%", "200%", "400%", "800%", "1200%", "1800%", "2500%"};
				float zoomVals[] = { -1.0f, 20.0f, 50.0f, 100.0f, 150.0f, 200.0f, 400.0f, 800.0f, 1200.0f, 1800.0f, 2500.0f };
				tString currZoomStr;
				tsPrintf(currZoomStr, "%0.0f%%", ZoomPercent);
				int zoomIdx = 0;
				if (ImGui::Combo(currZoomStr.ConstText(), &zoomIdx, zoomItems, IM_ARRAYSIZE(zoomItems)) && (zoomIdx > 0))
					ApplyZoomDelta( zoomVals[zoomIdx]-ZoomPercent, 1.0f, true);
				ImGui::PopItemWidth();

				ImGui::Separator();
				if (ImGui::Button("Reset Pan"))
					ResetPan();

				ImGui::PopStyleVar();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help"))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,3));
				ImGui::MenuItem("Cheat Sheet", "F1", &ShowCheatSheet, true);
				ImGui::MenuItem("About", "", &ShowAbout, true);
				ImGui::PopStyleVar();
				ImGui::EndMenu();
			}

			tColourf floatCol(PixelColour);
			ImVec4 colV4(floatCol.R, floatCol.G, floatCol.B, floatCol.A);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40.0f);			
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
			if (ImGui::ColorButton("Colour##2f", colV4, ImGuiColorEditFlags_RGB | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel, ImVec2(20,20)))
				ImGui::OpenPopup("CopyColourAs");

			if (ImGui::BeginPopup("CopyColourAs"))
				ColourCopyAs();

			bool buttonAvail = CurrImage ? !CurrImage->IsAltPictureEnabled() : false;
			if
			(
				ImGui::ImageButton(ImTextureID(FlipHImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2, ColourBG,
				buttonAvail ? ColourEnabledTint : ColourDisabledTint) && buttonAvail
			)
			{
				CurrImage->Unbind();
				CurrImage->Flip(true);
				CurrImage->Bind();
			}
			ShowToolTip("Flip Horizontally");

			if
			(
				ImGui::ImageButton(ImTextureID(FlipVImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2, ColourBG,
				buttonAvail ? ColourEnabledTint : ColourDisabledTint) && buttonAvail
			)
			{
				CurrImage->Unbind();
				CurrImage->Flip(false);
				CurrImage->Bind();
			}
			ShowToolTip("Flip Vertically");

			if
			(
				ImGui::ImageButton(ImTextureID(RotateACWImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2, ColourBG,
				buttonAvail ? ColourEnabledTint : ColourDisabledTint) && buttonAvail
			)
			{
				CurrImage->Unbind();
				CurrImage->Rotate90(true);
				CurrImage->Bind();
			}
			ShowToolTip("Rotate 90 Anticlockwise");

			if
			(
				ImGui::ImageButton(ImTextureID(RotateCWImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2, ColourBG,
				buttonAvail ? ColourEnabledTint : ColourDisabledTint) && buttonAvail
			)
			{
				CurrImage->Unbind();
				CurrImage->Rotate90(false);
				CurrImage->Bind();
			}
			ShowToolTip("Rotate 90 Clockwise");

			bool altMipmapsPicAvail = CurrImage ? CurrImage->IsAltMipmapsPictureAvail() : false;
			bool altMipmapsPicEnabl = altMipmapsPicAvail && CurrImage->IsAltPictureEnabled();
			if
			(
				ImGui::ImageButton
				(
					ImTextureID(MipmapsImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2,
					altMipmapsPicEnabl ? ColourPressedBG : ColourBG,
					altMipmapsPicAvail ? ColourEnabledTint : ColourDisabledTint
				) && altMipmapsPicAvail
			)
			{
				CurrImage->EnableAltPicture(!altMipmapsPicEnabl);
				CurrImage->Bind();
			}
			ShowToolTip("Display Mipmaps\nDDS files may include mipmaps.");

			bool altCubemapPicAvail = CurrImage ? CurrImage->IsAltCubemapPictureAvail() : false;
			bool altCubemapPicEnabl = altCubemapPicAvail && CurrImage->IsAltPictureEnabled();
			if
			(
				ImGui::ImageButton
				(
					ImTextureID(CubemapImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2,
					altCubemapPicEnabl ? ColourPressedBG : ColourBG,
					altCubemapPicAvail ? ColourEnabledTint : ColourDisabledTint
				) && altCubemapPicAvail
			)
			{
				CurrImage->EnableAltPicture(!altCubemapPicEnabl);
				CurrImage->Bind();
			}
			ShowToolTip("Display Cubemap\nDDS files may be cubemaps.");

			if
			(
				ImGui::ImageButton(ImTextureID(TileImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2,
				Config.Tile ? ColourPressedBG : ColourBG, ImVec4(1.00f, 1.00f, 1.00f, 1.00f))
			)
			{
				Config.Tile = !Config.Tile;
				if (!Config.Tile)
					ResetPan();
			}
			ShowToolTip("Show Images Tiled");

			bool recycleAvail = CurrImage ? true : false;
			if
			(
				ImGui::ImageButton
				(
					ImTextureID(RecycleImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2, ImVec4(0.00f, 0.00f, 0.00f, 0.00f),
					recycleAvail ? ColourEnabledTint : ColourDisabledTint
				) && recycleAvail
			)
			{
				DoDeleteFile = true;
			}
			ShowToolTip("Delete Current File");

			if
			(
				ImGui::ImageButton(ImTextureID(ContentViewImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2,
				Config.ContentViewShow ? ColourPressedBG : ColourBG, ImVec4(1.00f, 1.00f, 1.00f, 1.00f))
			)
			{
				Config.ContentViewShow = !Config.ContentViewShow;
			}
			ShowToolTip("Content Thumbnail View");

			if
			(
				ImGui::ImageButton(ImTextureID(InfoOverlayImage.Bind()), ImVec2(16,16), ImVec2(0,1), ImVec2(1,0), 2,
				Config.InfoOverlayShow ? ColourPressedBG : ColourBG, ImVec4(1.00f, 1.00f, 1.00f, 1.00f))
			)
			{
				Config.InfoOverlayShow = !Config.InfoOverlayShow;
			}
			ShowToolTip("Information Overlay");
		}

		ImGui::EndMainMenuBar();
		ImGui::PopStyleVar();
	}

	if (!FullscreenMode && Config.ShowLog)
		DrawTextureViewerLog(0.0f, float(disph - bottomUIHeight), float(dispw), float(bottomUIHeight));

	// We allow the overlay and cheatsheet in fullscreen.
	if (Config.InfoOverlayShow)
		ShowInfoOverlay(&Config.InfoOverlayShow, 0.0f, float(topUIHeight), float(dispw), float(disph - bottomUIHeight - topUIHeight), imgxi, imgyi, ZoomPercent);

	if (!FullscreenMode && Config.ContentViewShow)
		ShowContentViewDialog(&Config.ContentViewShow);

	if (ShowCheatSheet)
		ShowCheatSheetPopup(&ShowCheatSheet);

	if (ShowAbout)
		ShowAboutPopup(&ShowAbout);

	if (DoDeleteFile)
	{
		DoDeleteFile = false;
		if (!Config.ConfirmDeletes)
			DeleteImageFile(CurrImage->Filename);
		else
			ImGui::OpenPopup("Delete File");
	}
	ShowDeleteFileModal();

	ImGui::Render();
	glViewport(0, 0, dispw, disph);
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

	glfwMakeContextCurrent(window);
	glfwSwapBuffers(window);

	// We're done the frame. Is slideshow playing.
	SlideshowCountdown -= dt;
	if (SlideshowPlaying && (SlideshowCountdown <= 0.0f))
	{
		OnNext(true);
		SlideshowCountdown = Config.SlidehowFrameDuration;
	}
}


void TexView::ShowDeleteFileModal()
{
	if (ImGui::BeginPopupModal("Delete File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		tString fullname = CurrImage->Filename;
		tString file = tSystem::tGetFileName(fullname);
		tString dir = tSystem::tGetDir(fullname);
		ImGui::Text("Confirm deletion of file\n%s\nin folder\n%s\n\n", file.ConstText(), dir.ConstText());

		ImGui::Separator();
		ImGui::Checkbox("Confirm next time", &Config.ConfirmDeletes);
		ImGui::Text("\n");

		if (ImGui::Button("Cancel", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();

		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();

		if (ImGui::Button("OK", ImVec2(120, 0)))
		{
			DeleteImageFile(fullname);
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}


bool TexView::DeleteImageFile(const tString& imgFile)
{
	tString nextImgFile = CurrImage->Next() ? CurrImage->Next()->Filename : tString();

	bool deleted = tSystem::tDeleteFile(imgFile, true, true);
	if (deleted)
	{
		ImageFileParam.Param = nextImgFile;		// We set this so if we lose and gain focus, we go back to the current image.
		PopulateImages();
		SetCurrentImage(nextImgFile);
	}

	return deleted;
}


bool TexView::ChangeScreenMode(bool fullscreen, bool force)
{
	if (!force && (FullscreenMode == fullscreen))
		return false;

	// If currently in windowed mode, remember our window geometry.
	if (!force && !FullscreenMode)
	{
		glfwGetWindowPos(TexView::Window, &TexView::Config.WindowX, &TexView::Config.WindowY);
		glfwGetWindowSize(TexView::Window, &TexView::Config.WindowW, &TexView::Config.WindowH);
	}

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);
	glfwWindowHint(GLFW_RED_BITS, mode->redBits);
	glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
	glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
	glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
	if (fullscreen)
		glfwSetWindowMonitor(TexView::Window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
	else
		glfwSetWindowMonitor(TexView::Window, nullptr, TexView::Config.WindowX, TexView::Config.WindowY, TexView::Config.WindowW, TexView::Config.WindowH, mode->refreshRate);

	FullscreenMode = fullscreen;
	return true;
}


void TexView::ApplyZoomDelta(float zoomDelta, float roundTo, bool correctPan)
{
	CurrZoomMode = ZoomMode::User;
	float zoomOrig = ZoomPercent;
	ZoomPercent += zoomDelta;
	if (((zoomOrig < 100.0f) && (ZoomPercent > 100.0f)) || ((zoomOrig > 100.0f) && (ZoomPercent < 100.0f)))
		ZoomPercent = 100.0f;

	//ZoomPercent = tMath::tRound(ZoomPercent*roundTo) / roundTo;
	tMath::tClamp(ZoomPercent, ZoomMin, ZoomMax);

	if (correctPan)
	{
		PanOffsetX += DragDownOffsetX; DragDownOffsetX = 0;
		PanOffsetY += DragDownOffsetY; DragDownOffsetY = 0;
		PanOffsetX = int(float(PanOffsetX)*ZoomPercent/zoomOrig);
		PanOffsetY = int(float(PanOffsetY)*ZoomPercent/zoomOrig);
	}
}


void TexView::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int modifiers)
{
	if ((action != GLFW_PRESS) && (action != GLFW_REPEAT))
		return;

	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput)
		return;

	switch (key)
	{
		case GLFW_KEY_LEFT:
			if (modifiers == GLFW_MOD_CONTROL)
				OnSkipBegin();
			else
				OnPrevious();
			break;

		case GLFW_KEY_RIGHT:
			if (modifiers == GLFW_MOD_CONTROL)
				OnSkipEnd();
			else
				OnNext();
			break;

		case GLFW_KEY_SPACE:
			OnNext();
			break;

		case GLFW_KEY_EQUAL:
			// Ctrl +
			if (modifiers == GLFW_MOD_CONTROL)
				ApplyZoomDelta(tMath::tRound(ZoomPercent*0.1f), 1.0f, true);
			break;

		case GLFW_KEY_MINUS:
			// Ctrl -
			if (modifiers == GLFW_MOD_CONTROL)
				ApplyZoomDelta(tMath::tRound(ZoomPercent*(0.909090909f - 1.0f)), 1.0f, true);
			break;

		case GLFW_KEY_ENTER:
			if (modifiers == GLFW_MOD_ALT)
				ChangeScreenMode(!FullscreenMode);
			break;

		case GLFW_KEY_F1:
			ShowCheatSheet = !ShowCheatSheet;
			break;

		case GLFW_KEY_F11:
			ChangeScreenMode(!FullscreenMode);
			break;

		case GLFW_KEY_ESCAPE:
			ChangeScreenMode(false);
			break;

		case GLFW_KEY_DELETE:
			DoDeleteFile = true;
			break;

		case GLFW_KEY_T:
			Config.Tile = !Config.Tile;
			if (!Config.Tile)
				ResetPan();
			break;

		case GLFW_KEY_L:
			Config.ShowLog = !Config.ShowLog;
			break;

		case GLFW_KEY_I:
			TexView::Config.InfoOverlayShow = !TexView::Config.InfoOverlayShow;
			break;

		case GLFW_KEY_V:
			TexView::Config.ContentViewShow = !TexView::Config.ContentViewShow;
			break;

		case GLFW_KEY_F:
			ResetPan();
			CurrZoomMode = ZoomMode::Fit;
			break;

		case GLFW_KEY_D:
			ResetPan();
			CurrZoomMode = ZoomMode::Downscale;
			break;

		case GLFW_KEY_Z:
			ZoomPercent = 100.0f;
			ResetPan();
			CurrZoomMode = ZoomMode::OneToOne;
			break;

		case GLFW_KEY_S:
			if (CurrImage)
			{
				JustOpenedSaveAsDialog = !SaveAsDialog;
				SaveAsDialog = !SaveAsDialog;
			}
			break;

		case GLFW_KEY_C:
			if (Images.GetNumItems() > 1)
			{
				JustOpenedContactDialog = !ContactDialog;
				ContactDialog = !ContactDialog;
			}
			break;

		case GLFW_KEY_P:
			PrefsDialog = !PrefsDialog;
			break;
	}
}


void TexView::MouseButtonCallback(GLFWwindow* window, int mouseButton, int press, int mods)
{
	DisappearCountdown = DisappearDuration;
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
		return;

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	if ((ypos <= double(TopUIHeight)) && !FullscreenMode)
		return;

	bool down = press ? true : false;
	switch (mouseButton)
	{
		// Left mouse button.
		case 0:
		{
			if (down)
			{
				CursorX = int(xpos);
				CursorY = int(ypos);
			}
			break;
		}

		// Right mouse button.
		case 1:
		{
			RMBDown = down;
			if (RMBDown)
			{
				DragAnchorX = int(xpos);
				DragAnchorY = int(ypos);
				PanOffsetX += DragDownOffsetX;
				PanOffsetY += DragDownOffsetY;
				DragDownOffsetX = 0;
				DragDownOffsetY = 0;
			}
			break;
		}
	}
}


void TexView::CursorPosCallback(GLFWwindow* window, double x, double y)
{
	DisappearCountdown = DisappearDuration;
}


void TexView::ScrollWheelCallback(GLFWwindow* window, double x, double y)
{
	DisappearCountdown = DisappearDuration;

	CurrZoomMode = ZoomMode::User;
	float percentChange = (y > 0.0) ? 0.1f : 1.0f-0.909090909f;
	float zoomDelta = ZoomPercent * percentChange * float(y);
	ApplyZoomDelta(zoomDelta, 10.0f, true);
}


void TexView::FileDropCallback(GLFWwindow* window, int count, const char** files)
{
	if (count < 1)
		return;

	tString file = tString(files[0]);
	tString path = tSystem::tGetDir(file);

	ImageFileParam.Param = file;
	PopulateImages();
	SetCurrentImage(file);
}


void TexView::FocusCallback(GLFWwindow* window, int gotFocus)
{
	if (!gotFocus)
		return;

	// If we got focus, rescan the current folder to see if the hash is different.
	tList<tStringItem> files;
	FindImageFiles(files);
	tuint256 hash = ComputeImagesHash(files);

	if (hash != ImagesHash)
	{
		tPrintf("Hash mismatch. Dir contents changed. Resynching.\n");
		PopulateImages();
		if (ImageFileParam.IsPresent())
			SetCurrentImage(TexView::ImageFileParam.Get());
		else
			SetCurrentImage();
	}
	else
	{
		tPrintf("Hash match. Dir contents same. Doing nothing.\n");
	}
}


void TexView::IconifyCallback(GLFWwindow* window, int iconified)
{
	WindowIconified = iconified;
}


/////////////////////////////
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

void thread_function()
{
	for (int i = 0; i < 10; i++)
	{
		tPrintf("H%d", i);
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}
 

int main(int argc, char** argv)
{
	tSystem::tSetStdoutRedirectCallback(TexView::PrintRedirectCallback);
	tCommand::tParse(argc, argv);

	// Setup window
	glfwSetErrorCallback(TexView::GlfwErrorCallback);
	if (!glfwInit())
		return 1;

	//std::mutex mutx;
	//mutx.lock();

	/* Thread testing using non-locking atomic.
	std::atomic_flag threadRunning = ATOMIC_FLAG_INIT;

	threadRunning.test_and_set();
	std::thread threadObj([&threadRunning] { thread_function(); threadRunning.clear(); });

	tPrintf("Main thread test.\n");
	while (1)
	{
		if (threadRunning.test_and_set())
		{
			tPrintf(".");
		}
		else
		{
			tPrintf("\nMain thread test. Helper thread finished.\n");
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	threadObj.join();
	//threadObj.get_id();
	*/

	int glfwMajor = 0; int glfwMinor = 0; int glfwRev = 0;
	glfwGetVersion(&glfwMajor, &glfwMinor, &glfwRev);

	tPrintf("Tacit Viewer V %d.%d.%d\n", TexView::MajorVersion, TexView::MinorVersion, TexView::Revision);
	tPrintf("Tacent Library V %d.%d.%d\n", tVersion::Major, tVersion::Minor, tVersion::Revision);
	tPrintf("Dear ImGui V %s\n", IMGUI_VERSION);
	tPrintf("GLEW V %s\n", glewGetString(GLEW_VERSION));
	tPrintf("GLFW V %d.%d.%d\n", glfwMajor, glfwMinor, glfwRev);

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	tString dataDir = tSystem::tGetProgramDir() + "Data/";
	tString cfgFile = dataDir + "Settings.cfg";
	TexView::Config.Load(cfgFile, mode->width, mode->height);
	TexView::Window = glfwCreateWindow(TexView::Config.WindowW, TexView::Config.WindowH, "Tacit Viewer", nullptr, nullptr);
	if (!TexView::Window)
		return 1;

	glfwSetWindowPos(TexView::Window, TexView::Config.WindowX, TexView::Config.WindowY);

	// Make the window title bar show up in black.
	HWND hwnd = glfwGetWin32Window(TexView::Window);
	const int DWMWA_USE_IMMERSIVE_DARK_MODE = 19;
	BOOL isDarkMode = 1;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDarkMode, sizeof(isDarkMode));
	ShowWindow(hwnd, SW_HIDE);
	ShowWindow(hwnd, SW_SHOW);

	if (!tSystem::tDirExists(dataDir))
	{
		::MessageBoxA
		(
			hwnd,
			"Tacit Texture Viewer failed to launch because it was run from a location "
			"that did not have the Data directory in it. The executable should be in the "
			"same place as the Data directory.",
			"Viewer Message",
			MB_OK
		);

		glfwDestroyWindow(TexView::Window);
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(TexView::Window);
	glfwSwapInterval(1); // Enable vsync
	glfwSetWindowRefreshCallback(TexView::Window, TexView::WindowRefreshFun);
	glfwSetKeyCallback(TexView::Window, TexView::KeyCallback);
	glfwSetMouseButtonCallback(TexView::Window, TexView::MouseButtonCallback);
	glfwSetCursorPosCallback(TexView::Window, TexView::CursorPosCallback);
	glfwSetScrollCallback(TexView::Window, TexView::ScrollWheelCallback);
	glfwSetDropCallback(TexView::Window, TexView::FileDropCallback);
	glfwSetWindowFocusCallback(TexView::Window, TexView::FocusCallback);
	glfwSetWindowIconifyCallback(TexView::Window, TexView::IconifyCallback);

	GLenum err = glewInit();
	if (err != GLEW_OK)
		return err;

	// Setup Dear ImGui context.
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	io.IniFilename = nullptr;
	io.ConfigFlags = 0;
	// io.NavActive = false;
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// Setup Dear ImGui style.
	ImGui::StyleColorsDark();

	// Setup platform/renderer bindings.
	ImGui_ImplGlfw_InitForOpenGL(TexView::Window, true);
	ImGui_ImplOpenGL2_Init();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	tString fontFile = dataDir + "Roboto-Medium.ttf";
	io.Fonts->AddFontFromFileTTF(fontFile.ConstText(), 14.0f);

	TexView::CursorImage.Load(dataDir + "PixelCursor.png");
	TexView::PrevImage.Load(dataDir + "PrevArrow.png");
	TexView::NextImage.Load(dataDir + "NextArrow.png");
	TexView::FlipHImage.Load(dataDir + "FlipH.png");
	TexView::FlipVImage.Load(dataDir + "FlipV.png");
	TexView::RotateACWImage.Load(dataDir + "RotACW.png");
	TexView::RotateCWImage.Load(dataDir + "RotCW.png");
	TexView::FullscreenImage.Load(dataDir + "Fullscreen.png");
	TexView::WindowedImage.Load(dataDir + "Windowed.png");
	TexView::SkipBeginImage.Load(dataDir + "SkipBegin.png");
	TexView::SkipEndImage.Load(dataDir + "SkipEnd.png");
	TexView::MipmapsImage.Load(dataDir + "Mipmaps.png");
	TexView::CubemapImage.Load(dataDir + "Cubemap.png");
	TexView::RecycleImage.Load(dataDir + "Recycle.png");
	TexView::InfoOverlayImage.Load(dataDir + "InfoOverlay.png");
	TexView::TileImage.Load(dataDir + "Tile.png");
	TexView::StopImage.Load(dataDir + "Stop.png");
	TexView::PlayImage.Load(dataDir + "Play.png");
	TexView::ContentViewImage.Load(dataDir + "ContentView.png");
	TexView::DefaultThumbnailImage.Load(dataDir + "DefaultThumbnail.png");

	TexView::PopulateImages();
	if (TexView::ImageFileParam.IsPresent())
		TexView::SetCurrentImage(TexView::ImageFileParam.Get());
	else
		TexView::SetCurrentImage();

	// Main loop.
	static double lastUpdateTime = glfwGetTime();
	while (!glfwWindowShouldClose(TexView::Window))
	{
		double currUpdateTime = glfwGetTime();
		TexView::Update(TexView::Window, currUpdateTime - lastUpdateTime);
		lastUpdateTime = currUpdateTime;
	}

	// Get current window geometry and set in config file if we're not in fullscreen mode or iconified.
	if (!TexView::FullscreenMode && !TexView::WindowIconified)
	{
		glfwGetWindowPos(TexView::Window, &TexView::Config.WindowX, &TexView::Config.WindowY);
		glfwGetWindowSize(TexView::Window, &TexView::Config.WindowW, &TexView::Config.WindowH);
	}
	TexView::Config.Save(cfgFile);

	// Cleanup.
	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(TexView::Window);
	glfwTerminate();
	return 0;
}
