
#define no_init_all deprecated
#include <Windows.h>
#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sstream>

// glad includes
#include <glad/gl.h>
#include <glad/wgl.h>
#pragma warning(disable : 4996)

//-------------------------------------------------------------
// Window Handling
//-------------------------------------------------------------
struct Win32PlatState
{
	FILE *log_fp;
	HINSTANCE hinstOpenGL;
	HWND appWindow;
	HDC appDC;
	HINSTANCE appInstance;
	HGLRC hGLRC;
	DWORD processId;
	bool renderThreadBlocked;
	int nMessagesPerFrame;		///< The max number of messages to dispatch per frame
	HMENU appMenu;				///< The menu bar for the window
#ifdef UNICODE
   //HIMC imeHandle;
#endif

	int desktopBitsPixel;
	int desktopWidth;
	int desktopHeight;
	int desktopClientWidth;
	int desktopClientHeight;
	int currentTime;
	
	// minimum time per frame
	UINT32 sleepTicks;
	// are we in the background?
	bool backgrounded;

	Win32PlatState();
};

Win32PlatState::Win32PlatState()
{
	log_fp = NULL;
	hinstOpenGL = NULL;
	appDC = NULL;
	appInstance = NULL;
	currentTime = 0;
	processId = 0;
}

Win32PlatState winState;

void CreatePixelFormat(PIXELFORMATDESCRIPTOR *pPFD, int colorBits, int depthBits, int stencilBits, bool stereo)
{
	PIXELFORMATDESCRIPTOR src =
	{
	   sizeof(PIXELFORMATDESCRIPTOR),   // size of this pfd
	   1,                      // version number
	   PFD_DRAW_TO_WINDOW |    // support window
	   PFD_SUPPORT_OPENGL |    // support OpenGL
	   PFD_DOUBLEBUFFER,       // double buffered
	   PFD_TYPE_RGBA,          // RGBA type
	   colorBits,              // color depth
	   0, 0, 0, 0, 0, 0,       // color bits ignored
	   0,                      // no alpha buffer
	   0,                      // shift bit ignored
	   0,                      // no accumulation buffer
	   0, 0, 0, 0,             // accum bits ignored
	   depthBits,              // depth buffer
	   stencilBits,            // stencil buffer
	   0,                      // no auxiliary buffer
	   PFD_MAIN_PLANE,         // main layer
	   0,                      // reserved
	   0, 0, 0                 // layer masks ignored
	};

	if (stereo)
	{
		//ri.Printf( PRINT_ALL, "...attempting to use stereo\n" );
		src.dwFlags |= PFD_STEREO;
		//glConfig.stereoEnabled = true;
	}
	else
	{
		//glConfig.stereoEnabled = qfalse;
	}
	*pPFD = src;
}

#define dT(s)    L##s

LPCWSTR windowClassName = L"Renderer Window";
LPCWSTR windowName = L"Renderer Tut Window";

struct Resolution
{
	UINT32 w, h, bpp;

	Resolution(UINT32 _w = 0, UINT32 _h = 0, UINT32 _bpp = 0)
	{
		w = _w;
		h = _h;
		bpp = _bpp;
	}

	bool operator==(const Resolution& otherRes) const
	{
		return ((w == otherRes.w) && (h == otherRes.h) && (bpp == otherRes.bpp));
	}

	void operator=(const Resolution& otherRes)
	{
		w = otherRes.w;
		h = otherRes.h;
		bpp = otherRes.bpp;
	}
};

Resolution getDesktopResolution()
{
	Resolution  Result;
	RECT        clientRect;
	HWND        hDesktop = GetDesktopWindow();
	HDC         hDeskDC = GetDC(hDesktop);

	// Retrieve Resolution Information.
	Result.bpp = winState.desktopBitsPixel = GetDeviceCaps(hDeskDC, BITSPIXEL);
	Result.w = winState.desktopWidth = GetDeviceCaps(hDeskDC, HORZRES);
	Result.h = winState.desktopHeight = GetDeviceCaps(hDeskDC, VERTRES);

	// Release Device Context.
	ReleaseDC(hDesktop, hDeskDC);

	SystemParametersInfo(SPI_GETWORKAREA, 0, &clientRect, 0);
	winState.desktopClientWidth = clientRect.right;
	winState.desktopClientHeight = clientRect.bottom;

	// Return Result;
	return Result;
}

HWND CreateOpenGLWindow(UINT32 width, UINT32 height, bool fullScreen, bool allowSizing)
{
	int windowStyle = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	int exWindowStyle = 0;

	if (fullScreen)
		windowStyle |= (WS_POPUP | WS_MAXIMIZE);
	else
		if (!allowSizing)
			windowStyle |= (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
		else
			windowStyle |= (WS_OVERLAPPEDWINDOW);

	return CreateWindowEx(
		exWindowStyle,
		windowClassName,
		windowName,
		windowStyle,
		0, 0, width, height,
		NULL, NULL,
		winState.appInstance,
		NULL);
}

static bool sgQueueEvents;
struct WinMessage
{
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	WinMessage() {};
	WinMessage(UINT m, WPARAM w, LPARAM l) : message(m), wParam(w), lParam(l) {}
};
std::vector<WinMessage> sgWinMessages;

static LRESULT PASCAL WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CLOSE:
		DestroyWindow(hWnd);
		PostQuitMessage(0);
		return(0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return(0);
		break;
	default:
		{
			if (sgQueueEvents)
			{
				WinMessage msg(message, wParam, lParam);
				sgWinMessages.push_back(msg);
			}
		}
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

static void InitWindowClass()
{
	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));

	wc.style = CS_OWNDC;
	wc.lpfnWndProc = WindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = winState.appInstance;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = windowClassName;
	RegisterClass(&wc);

	// Curtain window class:
	wc.lpfnWndProc = DefWindowProc;
	wc.hCursor = NULL;
	wc.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
	wc.lpszClassName = dT("Curtain");
	RegisterClass(&wc);
}

static void InitWindow()
{
	winState.processId = GetCurrentProcessId();
}

//-------------------------------------------------------------
// Opengl loading
//-------------------------------------------------------------
struct GFXAdapterLUID
{
	unsigned long LowPart;
	long HighPart;
};

struct GFXDevice {
	char mName[512];
	char mOutputName[512];
	GFXAdapterLUID mLUID;
	const char *getName() const { return mName; }
	const char *getOutputName() const { return mOutputName; }
	UINT32 mIndex;

	GFXDevice()
	{
		mName[0] = 0;
		mOutputName[0] = 0;
		mIndex = 0;
		memset(&mLUID, '\0', sizeof(mLUID));
	}
	~GFXDevice() {}

};

void* mContext;

#define gglHasWExtension(EXTENSION) GLAD_WGL_##EXTENSION

void gglPerformBinds()
{
	if (!gladLoaderLoadGL())
	{
		return;
	}
}

void gglPerformExtensionBinds(void *context)
{
	if (!gladLoaderLoadWGL((HDC)context))
	{
		return;
	}
}

void loadGLCore()
{
	static bool coreLoaded = false; // Guess what this is for.
	if (coreLoaded)
		return;
	coreLoaded = true;

	// Make sure we've got our GL bindings.
	gglPerformBinds();
}

void loadGLExtensions(void *context)
{
	static bool extensionsLoaded = false;
	if (extensionsLoaded)
		return;
	extensionsLoaded = true;

	gglPerformExtensionBinds(context);
}

GFXDevice* initOpenGL()
{
	WNDCLASS windowClass;
	memset(&windowClass, 0, sizeof(WNDCLASS));

	windowClass.lpszClassName = L"GFX-OpenGL";
	windowClass.style = CS_OWNDC;
	windowClass.lpfnWndProc = DefWindowProc;
	windowClass.hInstance = winState.appInstance;

	if (!RegisterClass(&windowClass))
		assert(false, "Failed to register the window.");

	// create test window.
	HWND hwnd = CreateWindow(L"GFX-OpenGL", L"", WS_POPUP, 0, 0, 640, 480,
		NULL, NULL, winState.appInstance, NULL);

	assert(hwnd != NULL, "Failed to create test window");

	// create device context
	HDC tempDC = GetDC(hwnd);
	assert(tempDC != NULL, "Failed to create device context.");

	PIXELFORMATDESCRIPTOR pfd; 
	CreatePixelFormat(&pfd, 32, 0, 0, false);
	if (!SetPixelFormat(tempDC, ChoosePixelFormat(tempDC, &pfd), &pfd))
		assert(false, "Failed to set pixel format");

	// rendering context
	HGLRC tempGLRC = wglCreateContext(tempDC);
	if (!wglMakeCurrent(tempDC, tempGLRC))
		assert(false, "Failed to create windows rendering context and make it current.");

	// Add the GL renderer
	loadGLCore();
	loadGLExtensions(tempDC);

	GFXDevice *toAdd = new GFXDevice();
	toAdd->mIndex = 0;

	const char* renderer = (const char*)glGetString(GL_RENDERER);
	assert(renderer != NULL, "GL_RENDERER returned NULL!");
	if (renderer)
	{
		strncpy(toAdd->mName, renderer, 512);
		strncat(toAdd->mName, " OpenGL", 512);
	}
	else
		strncpy(toAdd->mName, "OpenGL", 512);

	// Cleanup our window
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(tempGLRC);
	ReleaseDC(hwnd, tempDC);
	DestroyWindow(hwnd);
	UnregisterClass(L"GFX-OpenGL", winState.appInstance);

	return toAdd;
}

void initCanonicalGLState()
{
	const char* vendorStr = (const char*)glGetString(GL_VENDOR);
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glEnable(GL_FRAMEBUFFER_SRGB);

}

void initFinalState(HWND window)
{
	// Create a device context
	HDC hdcGL = GetDC(window);
	assert(hdcGL != NULL, "Failed to create device context");
	winState.appDC = hdcGL;

	// Create pixel format descriptor...
	PIXELFORMATDESCRIPTOR pfd;
	CreatePixelFormat(&pfd, 32, 0, 0, false); // 32 bit color... We do not need depth or stencil, OpenGL renders into a FBO and then copy the image to window
	if (!SetPixelFormat(hdcGL, ChoosePixelFormat(hdcGL, &pfd), &pfd))
		assert(false, "cannot get the one and only pixel format we check for.");

	int OGL_MAJOR = 3;
	int OGL_MINOR = 2;

#if _DEBUG
	int debugFlag = WGL_CONTEXT_DEBUG_BIT_ARB;
#else
	int debugFlag = 0;
#endif

	// Create a temp rendering context, needed a current context to use wglCreateContextAttribsARB
	HGLRC tempGLRC = wglCreateContext(hdcGL);
	if (!wglMakeCurrent(hdcGL, tempGLRC))
		assert(false, "Couldn't make temp GL context.");

	if (gglHasWExtension(ARB_create_context))
	{
		int const create_attribs[] = {
				 WGL_CONTEXT_MAJOR_VERSION_ARB, OGL_MAJOR,
				 WGL_CONTEXT_MINOR_VERSION_ARB, OGL_MINOR,
				 WGL_CONTEXT_FLAGS_ARB, /*WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB |*/ debugFlag,
				 WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
				 //WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
				 0
		};

		mContext = wglCreateContextAttribsARB(hdcGL, 0, create_attribs);
		if (!mContext)
		{
			assert(0, "");
		}
	}
	else
		mContext = wglCreateContext(hdcGL);

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(tempGLRC);
	if (!wglMakeCurrent(hdcGL, (HGLRC)mContext))
		assert(false, "Cannot make our context current.");

	loadGLCore();
	loadGLExtensions(hdcGL);

	initCanonicalGLState();

}

GLuint LoadShaders(const char * vertex_file_path, const char * fragment_file_path) {

	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	// Read the Vertex Shader code from the file
	std::string VertexShaderCode;
	std::ifstream VertexShaderStream(vertex_file_path, std::ios::in);
	if (VertexShaderStream.is_open()) {
		std::stringstream sstr;
		sstr << VertexShaderStream.rdbuf();
		VertexShaderCode = sstr.str();
		VertexShaderStream.close();
	}
	else {
		printf("Impossible to open %s. Are you in the right directory ? Don't forget to read the FAQ !\n", vertex_file_path);
		getchar();
		return 0;
	}

	// Read the Fragment Shader code from the file
	std::string FragmentShaderCode;
	std::ifstream FragmentShaderStream(fragment_file_path, std::ios::in);
	if (FragmentShaderStream.is_open()) {
		std::stringstream sstr;
		sstr << FragmentShaderStream.rdbuf();
		FragmentShaderCode = sstr.str();
		FragmentShaderStream.close();
	}

	GLint Result = GL_FALSE;
	int InfoLogLength;


	// Compile Vertex Shader
	printf("Compiling shader : %s\n", vertex_file_path);
	char const * VertexSourcePointer = VertexShaderCode.c_str();
	glShaderSource(VertexShaderID, 1, &VertexSourcePointer, NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> VertexShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, &VertexShaderErrorMessage[0]);
		printf("%s\n", &VertexShaderErrorMessage[0]);
	}



	// Compile Fragment Shader
	printf("Compiling shader : %s\n", fragment_file_path);
	char const * FragmentSourcePointer = FragmentShaderCode.c_str();
	glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer, NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> FragmentShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, &FragmentShaderErrorMessage[0]);
		printf("%s\n", &FragmentShaderErrorMessage[0]);
	}



	// Link the program
	printf("Linking program\n");
	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> ProgramErrorMessage(InfoLogLength + 1);
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		printf("%s\n", &ProgramErrorMessage[0]);
	}


	glDetachShader(ProgramID, VertexShaderID);
	glDetachShader(ProgramID, FragmentShaderID);

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	return ProgramID;
}

//-------------------------------------------------------------
// Main loading
//-------------------------------------------------------------

int main()
{
	winState.appInstance = GetModuleHandle(NULL);
}

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
	winState.appInstance = hInstance;
	InitWindowClass();
	getDesktopResolution();
	GFXDevice* dev = new GFXDevice();
	dev = initOpenGL();
	InitWindow();

	HWND window;
	window = CreateOpenGLWindow(1280, 720,false,true);
	initFinalState(window);
	ShowWindow(window, SW_SHOW);

	if (gglHasWExtension(EXT_swap_control))
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (gglHasWExtension(EXT_swap_control_tear))
		{
			bool ret = wglSwapIntervalEXT(1);
			if (!ret)
				wglSwapIntervalEXT(1);
		}
		else
		{
			wglSwapIntervalEXT(0);
		}

	}
	else
	{
		wglSwapIntervalEXT(0);
	}

	MSG msg = {};
	sgQueueEvents = true;

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	GLuint programID = LoadShaders("TransformVertexShader.vertexshader", "ColorFragmentShader.fragmentshader");

	GLuint MatrixID = glGetUniformLocation(programID, "MVP");

	// bind our buffer data.
	static const GLfloat g_vertex_buffer_data[] = {
		-1.0f,-1.0f,-1.0f,
		-1.0f,-1.0f, 1.0f,
		-1.0f, 1.0f, 1.0f,
		 1.0f, 1.0f,-1.0f,
		-1.0f,-1.0f,-1.0f,
		-1.0f, 1.0f,-1.0f,
		 1.0f,-1.0f, 1.0f,
		-1.0f,-1.0f,-1.0f,
		 1.0f,-1.0f,-1.0f,
		 1.0f, 1.0f,-1.0f,
		 1.0f,-1.0f,-1.0f,
		-1.0f,-1.0f,-1.0f,
		-1.0f,-1.0f,-1.0f,
		-1.0f, 1.0f, 1.0f,
		-1.0f, 1.0f,-1.0f,
		 1.0f,-1.0f, 1.0f,
		-1.0f,-1.0f, 1.0f,
		-1.0f,-1.0f,-1.0f,
		-1.0f, 1.0f, 1.0f,
		-1.0f,-1.0f, 1.0f,
		 1.0f,-1.0f, 1.0f,
		 1.0f, 1.0f, 1.0f,
		 1.0f,-1.0f,-1.0f,
		 1.0f, 1.0f,-1.0f,
		 1.0f,-1.0f,-1.0f,
		 1.0f, 1.0f, 1.0f,
		 1.0f,-1.0f, 1.0f,
		 1.0f, 1.0f, 1.0f,
		 1.0f, 1.0f,-1.0f,
		-1.0f, 1.0f,-1.0f,
		 1.0f, 1.0f, 1.0f,
		-1.0f, 1.0f,-1.0f,
		-1.0f, 1.0f, 1.0f,
		 1.0f, 1.0f, 1.0f,
		-1.0f, 1.0f, 1.0f,
		 1.0f,-1.0f, 1.0f
	};

	// One color for each vertex. They were generated randomly.
	static const GLfloat g_color_buffer_data[] = {
		0.583f,  0.771f,  0.014f,
		0.609f,  0.115f,  0.436f,
		0.327f,  0.483f,  0.844f,
		0.822f,  0.569f,  0.201f,
		0.435f,  0.602f,  0.223f,
		0.310f,  0.747f,  0.185f,
		0.597f,  0.770f,  0.761f,
		0.559f,  0.436f,  0.730f,
		0.359f,  0.583f,  0.152f,
		0.483f,  0.596f,  0.789f,
		0.559f,  0.861f,  0.639f,
		0.195f,  0.548f,  0.859f,
		0.014f,  0.184f,  0.576f,
		0.771f,  0.328f,  0.970f,
		0.406f,  0.615f,  0.116f,
		0.676f,  0.977f,  0.133f,
		0.971f,  0.572f,  0.833f,
		0.140f,  0.616f,  0.489f,
		0.997f,  0.513f,  0.064f,
		0.945f,  0.719f,  0.592f,
		0.543f,  0.021f,  0.978f,
		0.279f,  0.317f,  0.505f,
		0.167f,  0.620f,  0.077f,
		0.347f,  0.857f,  0.137f,
		0.055f,  0.953f,  0.042f,
		0.714f,  0.505f,  0.345f,
		0.783f,  0.290f,  0.734f,
		0.722f,  0.645f,  0.174f,
		0.302f,  0.455f,  0.848f,
		0.225f,  0.587f,  0.040f,
		0.517f,  0.713f,  0.338f,
		0.053f,  0.959f,  0.120f,
		0.393f,  0.621f,  0.362f,
		0.673f,  0.211f,  0.457f,
		0.820f,  0.883f,  0.371f,
		0.982f,  0.099f,  0.879f
	};

	GLuint vertexbuffer;
	glGenBuffers(1, &vertexbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_buffer_data), g_vertex_buffer_data, GL_STATIC_DRAW);

	GLuint colorbuffer;
	glGenBuffers(1, &colorbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, colorbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_color_buffer_data), g_color_buffer_data, GL_STATIC_DRAW);

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		glClear(GL_COLOR_BUFFER_BIT);

		// DRAW HERE PLEASE!!!!!!
		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
		glVertexAttribPointer(
			0,                  // attribute. No particular reason for 0, but must match the layout in the shader.
			3,                  // size
			GL_FLOAT,           // type
			GL_FALSE,           // normalized?
			0,                  // stride
			(void*)0            // array buffer offset
		);

		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, colorbuffer);
		glVertexAttribPointer(
			1,                                // attribute. No particular reason for 1, but must match the layout in the shader.
			3,                                // size
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                                // stride
			(void*)0                          // array buffer offset
		);

		// Draw the triangle !
		glDrawArrays(GL_TRIANGLES, 0, 12 * 3); // 12*3 indices starting at 0 -> 12 triangles

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);

		SwapBuffers(winState.appDC);
	}

	sgQueueEvents = false;
	wglDeleteContext((HGLRC)mContext);
	ReleaseDC(window, winState.appDC);
	DestroyWindow(window);
}
