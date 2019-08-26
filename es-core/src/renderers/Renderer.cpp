#include "renderers/Renderer.h"

#include "math/Transform4x4f.h"
#include "math/Vector2i.h"
#include "resources/ResourceManager.h"
#include "ImageIO.h"
#include "Log.h"
#include "Settings.h"

#include <SDL.h>
#include <stack>

namespace Renderer
{
	static std::stack<Rect> clipStack;
	static std::stack<Rect> nativeClipStack;

	static SDL_Window*      sdlWindow          = nullptr;
	static int              windowWidth        = 0;
	static int              windowHeight       = 0;
	static int              screenWidth        = 0;
	static int              screenHeight       = 0;
	static int              screenOffsetX      = 0;
	static int              screenOffsetY      = 0;
	static int              screenRotate       = 0;
	static bool             initialCursorState = 1;

	static void setIcon()
	{
		size_t                     width   = 0;
		size_t                     height  = 0;
		ResourceData               resData = ResourceManager::getInstance()->getFileData(":/window_icon_256.png");
		std::vector<unsigned char> rawData = ImageIO::loadFromMemoryRGBA32(resData.ptr.get(), resData.length, width, height);

		if(!rawData.empty())
		{
			ImageIO::flipPixelsVert(rawData.data(), width, height);

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			unsigned int rmask = 0xFF000000;
			unsigned int gmask = 0x00FF0000;
			unsigned int bmask = 0x0000FF00;
			unsigned int amask = 0x000000FF;
#else
			unsigned int rmask = 0x000000FF;
			unsigned int gmask = 0x0000FF00;
			unsigned int bmask = 0x00FF0000;
			unsigned int amask = 0xFF000000;
#endif
			// try creating SDL surface from logo data
			SDL_Surface* logoSurface = SDL_CreateRGBSurfaceFrom((void*)rawData.data(), (int)width, (int)height, 32, (int)(width * 4), rmask, gmask, bmask, amask);

			if(logoSurface != nullptr)
			{
				SDL_SetWindowIcon(sdlWindow, logoSurface);
				SDL_FreeSurface(logoSurface);
			}
		}

	} // setIcon

	static bool createWindow()
	{
		LOG(LogInfo) << "Creating window...";

		if(SDL_Init(SDL_INIT_VIDEO) != 0)
		{
			LOG(LogError) << "Error initializing SDL!\n	" << SDL_GetError();
			return false;
		}

		initialCursorState = (SDL_ShowCursor(0) != 0);

		SDL_DisplayMode dispMode;
		SDL_GetDesktopDisplayMode(0, &dispMode);
		windowWidth   = Settings::getInstance()->getInt("WindowWidth")   ? Settings::getInstance()->getInt("WindowWidth")   : dispMode.w;
		windowHeight  = Settings::getInstance()->getInt("WindowHeight")  ? Settings::getInstance()->getInt("WindowHeight")  : dispMode.h;
		screenWidth   = Settings::getInstance()->getInt("ScreenWidth")   ? Settings::getInstance()->getInt("ScreenWidth")   : windowWidth;
		screenHeight  = Settings::getInstance()->getInt("ScreenHeight")  ? Settings::getInstance()->getInt("ScreenHeight")  : windowHeight;
		screenOffsetX = Settings::getInstance()->getInt("ScreenOffsetX") ? Settings::getInstance()->getInt("ScreenOffsetX") : 0;
		screenOffsetY = Settings::getInstance()->getInt("ScreenOffsetY") ? Settings::getInstance()->getInt("ScreenOffsetY") : 0;
		screenRotate  = Settings::getInstance()->getInt("ScreenRotate")  ? Settings::getInstance()->getInt("ScreenRotate")  : 0;

		setupWindow();

		unsigned int windowFlags = (Settings::getInstance()->getBool("Windowed") ? 0 : (Settings::getInstance()->getBool("FullscreenBorderless") ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_FULLSCREEN)) | getWindowFlags();

		if((sdlWindow = SDL_CreateWindow("EmulationStation", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, windowFlags)) == nullptr)
		{
			LOG(LogError) << "Error creating SDL window!\n\t" << SDL_GetError();
			return false;
		}

		LOG(LogInfo) << "Created window successfully.";

		createContext();
		setIcon();
		setSwapInterval();

		return true;

	} // createWindow

	static void destroyWindow()
	{
		destroyContext();

		SDL_DestroyWindow(sdlWindow);
		sdlWindow = nullptr;

		SDL_ShowCursor(initialCursorState);

		SDL_Quit();

	} // destroyWindow

	void activateWindow()
	{
		SDL_RaiseWindow(sdlWindow);
		SDL_SetWindowInputFocus(sdlWindow);
	}

	bool init()
	{
		if(!createWindow())
			return false;

		Transform4x4f projection = Transform4x4f::Identity();
		Rect          viewport   = Rect(0, 0, 0, 0);

		switch(screenRotate)
		{
			case 0:
			{
				viewport.x = screenOffsetX;
				viewport.y = screenOffsetY;
				viewport.w = screenWidth;
				viewport.h = screenHeight;

				projection.orthoProjection(0, screenWidth, screenHeight, 0, -1.0, 1.0);
			}
			break;

			case 1:
			{
				viewport.x = windowWidth - screenOffsetY - screenHeight;
				viewport.y = screenOffsetX;
				viewport.w = screenHeight;
				viewport.h = screenWidth;

				projection.orthoProjection(0, screenHeight, screenWidth, 0, -1.0, 1.0);
				projection.rotate((float)ES_DEG_TO_RAD(90), {0, 0, 1});
				projection.translate({0, screenHeight * -1.0f, 0});
			}
			break;

			case 2:
			{
				viewport.x = windowWidth  - screenOffsetX - screenWidth;
				viewport.y = windowHeight - screenOffsetY - screenHeight;
				viewport.w = screenWidth;
				viewport.h = screenHeight;

				projection.orthoProjection(0, screenWidth, screenHeight, 0, -1.0, 1.0);
				projection.rotate((float)ES_DEG_TO_RAD(180), {0, 0, 1});
				projection.translate({screenWidth * -1.0f, screenHeight * -1.0f, 0});
			}
			break;

			case 3:
			{
				viewport.x = screenOffsetY;
				viewport.y = windowHeight - screenOffsetX - screenWidth;
				viewport.w = screenHeight;
				viewport.h = screenWidth;

				projection.orthoProjection(0, screenHeight, screenWidth, 0, -1.0, 1.0);
				projection.rotate((float)ES_DEG_TO_RAD(270), {0, 0, 1});
				projection.translate({screenWidth * -1.0f, 0, 0});
			}
			break;
		}

		setViewport(viewport);
		setProjection(projection);
		swapBuffers();

		return true;

	} // init

	void deinit()
	{
		destroyWindow();

	} // deinit

	void pushClipRect(const Vector2i& _pos, const Vector2i& _size)
	{
		Rect box(_pos.x(), _pos.y(), _size.x(), _size.y());

		if(box.w == 0) box.w = screenWidth  - box.x;
		if(box.h == 0) box.h = screenHeight - box.y;

		switch(screenRotate)
		{
			case 0: { box = Rect(screenOffsetX + box.x,                       screenOffsetY + box.y,                        box.w, box.h); } break;
			case 1: { box = Rect(windowWidth - screenOffsetY - box.y - box.h, screenOffsetX + box.x,                        box.h, box.w); } break;
			case 2: { box = Rect(windowWidth - screenOffsetX - box.x - box.w, windowHeight - screenOffsetY - box.y - box.h, box.w, box.h); } break;
			case 3: { box = Rect(screenOffsetY + box.y,                       windowHeight - screenOffsetX - box.x - box.w, box.h, box.w); } break;
		}

		// make sure the box fits within clipStack.top(), and clip further accordingly
		if(clipStack.size())
		{
			const Rect& top = clipStack.top();
			if( top.x          >  box.x)          box.x = top.x;
			if( top.y          >  box.y)          box.y = top.y;
			if((top.x + top.w) < (box.x + box.w)) box.w = (top.x + top.w) - box.x;
			if((top.y + top.h) < (box.y + box.h)) box.h = (top.y + top.h) - box.y;
		}

		if(box.w < 0) box.w = 0;
		if(box.h < 0) box.h = 0;

		clipStack.push(box);
		nativeClipStack.push(Rect(_pos.x(), _pos.y(), _size.x(), _size.y()));

		setScissor(box);

	} // pushClipRect

	void popClipRect()
	{
		if(clipStack.empty())
		{
			LOG(LogError) << "Tried to popClipRect while the stack was empty!";
			return;
		}

		clipStack.pop();
		nativeClipStack.pop();

		if(clipStack.empty()) setScissor(Rect(0, 0, 0, 0));
		else                  setScissor(clipStack.top());

	} // popClipRect

	bool isClippingEnabled() { return !clipStack.empty(); }

	bool valueInRange(int value, int min, int max)
	{
		return (value >= min) && (value <= max);
	}

	bool rectOverlap(Rect &A, Rect &B)
	{
		bool xOverlap = valueInRange(A.x, B.x, B.x + B.w) ||
			valueInRange(B.x, A.x, A.x + A.w);

		bool yOverlap = valueInRange(A.y, B.y, B.y + B.h) ||
			valueInRange(B.y, A.y, A.y + A.h);

		return xOverlap && yOverlap;
	}

	bool isVisibleOnScreen(float x, float y, float w, float h)
	{
		Rect screen = Rect(0, 0, Renderer::getWindowWidth(), Renderer::getWindowHeight());
		Rect box = Rect(x, y, w, h);

		if (w > 0 && x + w <= 0)
			return false;

		if (h > 0 && y + h <= 0)
			return false;

		if (x == screen.w || y == screen.h)
			return false;

		if (!rectOverlap(box, screen))
			return false;

		if (clipStack.empty())
			return true;

		screen = nativeClipStack.top();
		return rectOverlap(screen, box);
	}

	void drawRect(const float _x, const float _y, const float _w, const float _h, const unsigned int _color, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		drawRect((int)Math::round(_x), (int)Math::round(_y), (int)Math::round(_w), (int)Math::round(_h), _color, _srcBlendFactor, _dstBlendFactor);

	} // drawRect

	void drawRect(const int _x, const int _y, const int _w, const int _h, const unsigned int _color, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		const unsigned int color = convertColor(_color);
		Vertex             vertices[4];

		vertices[0] = { { (float)(_x     ), (float)(_y     ) }, { 0.0f, 0.0f }, color };
		vertices[1] = { { (float)(_x     ), (float)(_y + _h) }, { 0.0f, 0.0f }, color };
		vertices[2] = { { (float)(_x + _w), (float)(_y     ) }, { 0.0f, 0.0f }, color };
		vertices[3] = { { (float)(_x + _w), (float)(_y + _h) }, { 0.0f, 0.0f }, color };

		bindTexture(0);
		drawTriangleStrips(vertices, 4, _srcBlendFactor, _dstBlendFactor);

	} // drawRect

	void drawGradientRect(int _x, int _y, int _w, int _h, unsigned int _color, unsigned int _colorBottom, bool _horz, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		const unsigned int color = convertColor(_color);
		const unsigned int colorBottom = convertColor(_colorBottom);

		Vertex             vertices[4];

		vertices[0] = { { (float)(_x), (float)(_y) }, { 0.0f, 0.0f }, _horz ? colorBottom : color };
		vertices[1] = { { (float)(_x), (float)(_y + _h) }, { 0.0f, 0.0f }, colorBottom };
		vertices[2] = { { (float)(_x + _w), (float)(_y) }, { 0.0f, 0.0f }, color };
		vertices[3] = { { (float)(_x + _w), (float)(_y + _h) }, { 0.0f, 0.0f }, _horz ? color : colorBottom };

		bindTexture(0);
		drawTriangleStrips(vertices, 4, _srcBlendFactor, _dstBlendFactor);
		/*
			   		 	  
		glEnable(GL_BLEND);
		glBlendFunc(blend_sfactor, blend_dfactor);

		glBegin(GL_QUADS);

		glColor4f(MAKEQUAD(horz ? colorBottom : color));
		glVertex2f(x, y);

		glColor4f(MAKEQUAD(color));
		glVertex2f(x + w, y);

		glColor4f(MAKEQUAD(horz ? color : colorBottom));
		glVertex2f(x + w, y + h);

		glColor4f(MAKEQUAD(colorBottom));
		glVertex2f(x, y + h);

		glEnd();

		glDisable(GL_BLEND);*/
	}

	SDL_Window* getSDLWindow()     { return sdlWindow; }
	int         getWindowWidth()   { return windowWidth; }
	int         getWindowHeight()  { return windowHeight; }
	int         getScreenWidth()   { return screenWidth; }
	int         getScreenHeight()  { return screenHeight; }
	int         getScreenOffsetX() { return screenOffsetX; }
	int         getScreenOffsetY() { return screenOffsetY; }
	int         getScreenRotate()  { return screenRotate; }

} // Renderer::
