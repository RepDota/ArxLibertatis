/*
 * Copyright 2011-2021 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARX_WINDOW_SDL2WINDOW_H
#define ARX_WINDOW_SDL2WINDOW_H

#include <SDL.h>

#include "platform/Platform.h"
#include "window/RenderWindow.h"

class SDL2InputBackend;

typedef enum {
	ARX_SDL_SYSWM_UNKNOWN,
	ARX_SDL_SYSWM_WINDOWS,
	ARX_SDL_SYSWM_X11,
	ARX_SDL_SYSWM_DIRECTFB,
	ARX_SDL_SYSWM_COCOA,
	ARX_SDL_SYSWM_UIKIT,
	ARX_SDL_SYSWM_WAYLAND,
	ARX_SDL_SYSWM_MIR,
	ARX_SDL_SYSWM_WINRT,
	ARX_SDL_SYSWM_ANDROID,
	ARX_SDL_SYSWM_VIVANTE,
	ARX_SDL_SYSWM_OS2,
} ARX_SDL_SYSWM_TYPE;

class SDL2Window final : public RenderWindow {
	
public:
	
	SDL2Window();
	~SDL2Window() override;
	
	bool initializeFramework() override;
	void setTitle(const std::string & title) override;
	bool setVSync(int vsync) override;
	void setFullscreenMode(const DisplayMode & mode) override;
	void setWindowSize(const Vec2i & size) override;
	bool setGamma(float gamma = 1.f) override;
	bool initialize() override;
	void processEvents(bool waitForEvent) override;
	
	void showFrame() override;
	
	void hide() override;
	
	void setMinimizeOnFocusLost(bool enabled) override;
	MinimizeSetting willMinimizeOnFocusLost() override;
	
	std::string getClipboardText() override;
	void setClipboardText(const std::string & text) override;
	
	void allowScreensaver(bool allowed) override;
	
	InputBackend * getInputBackend() override;
	
private:
	
	int createWindowAndGLContext(const char * profile);
	
	void changeMode(DisplayMode mode, bool fullscreen);
	void updateSize(bool force = false);
	
	void restoreGamma();
	
	static int SDLCALL eventFilter(void * userdata, SDL_Event * event);
	
	SDL_Window * m_window;
	SDL_GLContext m_glcontext;
	
	SDL2InputBackend * m_input;
	
	MinimizeSetting m_minimizeOnFocusLost;
	MinimizeSetting m_allowScreensaver;
	
	float m_gamma;
	bool m_gammaOverridden;
	u16 m_gammaRed[256];
	u16 m_gammaGreen[256];
	u16 m_gammaBlue[256];
	
	u32 m_sdlVersion;
	ARX_SDL_SYSWM_TYPE m_sdlSubsystem;
	
	static SDL2Window * s_mainWindow;
	
	friend class SDL2InputBackend;
};

#endif // ARX_WINDOW_SDL2WINDOW_H
