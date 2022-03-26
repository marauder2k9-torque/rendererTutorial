
#define no_init_all deprecated
#include <stdio.h>
#include <assert.h>
#include <chrono>
#include <vector>
#include <fstream>
#include <algorithm>
#include <sstream>

// glad includes
#include <glad/gl.h>
#include <glad/wgl.h>
#pragma warning(disable : 4996)

#include "math/matrix.h"

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
	printf("Pixel Formate: %d %d %d. \n", colorBits, depthBits, stencilBits);
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
	printf("Get desktop resolution.\n");
	Resolution  Result;
	RECT        clientRect;
	HWND        hDesktop = GetDesktopWindow();
	HDC         hDeskDC = GetDC(hDesktop);

	// Retrieve Resolution Information.
	Result.bpp = winState.desktopBitsPixel = GetDeviceCaps(hDeskDC, BITSPIXEL);
	Result.w = winState.desktopWidth = GetDeviceCaps(hDeskDC, HORZRES);
	Result.h = winState.desktopHeight = GetDeviceCaps(hDeskDC, VERTRES);

	printf("Resolution: %d %d %d\n", Result.w, Result.h, Result.bpp);
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
	printf("Create our opengl window. \n");
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
		FreeConsole();
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
#define gglHasExtension(EXTENSION) GLAD_GL_##EXTENSION

void gglPerformBinds()
{
	printf("Load Glad binds. \n");
	if (!gladLoaderLoadGL())
	{
		return;
	}
}

void gglPerformExtensionBinds(void *context)
{
	printf("Load WGL binds. \n");
	if (!gladLoaderLoadWGL((HDC)context))
	{
		return;
	}
}

void loadGLCore()
{
	printf("Load glad core.\n");
	static bool coreLoaded = false; // Guess what this is for.
	if (coreLoaded)
		return;
	coreLoaded = true;

	// Make sure we've got our GL bindings.
	gglPerformBinds();
}

void loadGLExtensions(void *context)
{
	printf("Load extensions for wgl. \n");
	static bool extensionsLoaded = false;
	if (extensionsLoaded)
		return;
	extensionsLoaded = true;

	gglPerformExtensionBinds(context);
}

GFXDevice* initOpenGL()
{
	printf("Temp init OpenGL for hardware initializing and capabilities.\n\n");
	printf("--------------------------------------------\n");
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

	GLint maj, min;
	glGetIntegerv(GL_MAJOR_VERSION, &maj);
	glGetIntegerv(GL_MINOR_VERSION, &min);

	const char* renderer = (const char*)glGetString(GL_RENDERER);
	assert(renderer != NULL, "GL_RENDERER returned NULL!");
	if (renderer)
	{
		strncpy(toAdd->mName, renderer, 512);
		strncat(toAdd->mName, " OpenGL", 512);
	}
	else
		strncpy(toAdd->mName, "OpenGL", 512);

	printf("Renderer: %s \n", toAdd->mName);
	printf("Capabilities:\n");
	printf("\tSupports: OPENGL %d.%d \n", maj, min);
	GLint maxTexSize;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
	printf("\tMax Texture Size: %d \n", maxTexSize);

	if (gglHasExtension(EXT_texture_filter_anisotropic))
	{
		printf("\tAnistropic filtering supported.\n");
	}
	if (gglHasExtension(ARB_buffer_storage))
	{
		printf("\tBuffer storage supported.\n");
	}
	if (gglHasExtension(ARB_texture_storage))
	{
		printf("\tTexture storage supported.\n");
	}
	if (gglHasExtension(ARB_copy_image))
	{
		printf("\tCopy image supported.\n");
	}
	if (gglHasExtension(ARB_vertex_attrib_binding))
	{
		printf("\tVertex attrib binding supported.\n");
	}
	printf("--------------------------------------------\n\n");

	// Cleanup our window
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(tempGLRC);
	ReleaseDC(hwnd, tempDC);
	DestroyWindow(hwnd);
	UnregisterClass(L"GFX-OpenGL", winState.appInstance);

	return toAdd;
}

void initFinalState(HWND window)
{
	printf("Actually make our opengl context we will use \n");
	// Create a device context
	HDC hdcGL = GetDC(window);
	assert(hdcGL != NULL, "Failed to create device context");
	winState.appDC = hdcGL;

	// Create pixel format descriptor...
	PIXELFORMATDESCRIPTOR pfd;
	CreatePixelFormat(&pfd, 32, 0, 0, false); // 32 bit color... We do not need depth or stencil, OpenGL renders into a FBO and then copy the image to window
	if (!SetPixelFormat(hdcGL, ChoosePixelFormat(hdcGL, &pfd), &pfd))
		assert(false, "cannot get the one and only pixel format we check for.");

	int OGL_MAJOR = 4;
	int OGL_MINOR = 3;

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
		printf("Impossible to open %s!\n", vertex_file_path);
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
	printf("Linking program\n\n");
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
	return WinMain(winState.appInstance, NULL, (PSTR)GetCommandLine(), SW_SHOW);
}

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
	InitWindowClass();
	Resolution res = getDesktopResolution();
	GFXDevice* dev = new GFXDevice();
	dev = initOpenGL();
	InitWindow();

	HWND window;
	window = CreateOpenGLWindow(res.w, res.h,false,true);
	initFinalState(window);
	ShowWindow(window, SW_SHOW);

	// setup vsync if we have it.
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

	// pointer to window messages.
	MSG msg = {};
	sgQueueEvents = true;

	printf("-------------------------\n");
	printf("LOAD SHADER\n");
	printf("-------------------------\n");
	GLuint programID = LoadShaders("TransformVertexShader.vertexshader", "ColorFragmentShader.fragmentshader");

	// get the id from our shader for our matrix uniform locations.
	GLuint modelID = glGetUniformLocation(programID, "model");
	GLuint viewID = glGetUniformLocation(programID, "view");
	GLuint projID = glGetUniformLocation(programID, "proj");

	// create our projection matrix.
	Matrix4 proj;
	proj.identity();
	proj.setFrustum(90.0f,(float)res.w / (float)res.h, 0.01f, 100.0f);
	printf("-------------------------\n");
	printf("PROJECTION MATRIX\n");
	printf("-------------------------\n");
	proj.printMatrix();

	// create our view matrix (our camera)
	Matrix4 view;
	view.identity();
	view.lookAt(Vector3(4.0f, 3.0f, -3.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
	printf("-------------------------\n");
	printf("VIEW MATRIX\n");
	printf("-------------------------\n");
	view.printMatrix();

	// create a model matrix.
	Matrix4 model;
	model.identity();
	printf("-------------------------\n");
	printf("MODEL MATRIX\n");
	printf("-------------------------\n");
	model.printMatrix();

	// create a box
	GLfloat boxVerts[] = {	 1, 1, 1,  -1, 1, 1,  -1,-1, 1,     // v0-v1-v2 (front)
							-1,-1, 1,   1,-1, 1,   1, 1, 1,     // v2-v3-v0

							1, 1, 1,   1,-1, 1,   1,-1,-1,      // v0-v3-v4 (right)
							1,-1,-1,   1, 1,-1,   1, 1, 1,      // v4-v5-v0

							1, 1, 1,   1, 1,-1,  -1, 1,-1,      // v0-v5-v6 (top)
							-1, 1,-1,  -1, 1, 1,   1, 1, 1,     // v6-v1-v0

							-1, 1, 1,  -1, 1,-1,  -1,-1,-1,     // v1-v6-v7 (left)
							-1,-1,-1,  -1,-1, 1,  -1, 1, 1,     // v7-v2-v1

							-1,-1,-1,   1,-1,-1,   1,-1, 1,     // v7-v4-v3 (bottom)
							1,-1, 1,  -1,-1, 1,  -1,-1,-1,      // v3-v2-v7

							1,-1,-1,  -1,-1,-1,  -1, 1,-1,      // v4-v7-v6 (back)
							-1, 1,-1,   1, 1,-1,   1,-1,-1 };	// v6-v5-v4


	// color array for box
	GLfloat boxColors[] = {	1, 1, 1,   1, 1, 0,   1, 0, 0,      // v0-v1-v2 (front)
							1, 0, 0,   1, 0, 1,   1, 1, 1,      // v2-v3-v0

							1, 1, 1,   1, 0, 1,   0, 0, 1,      // v0-v3-v4 (right)
							0, 0, 1,   0, 1, 1,   1, 1, 1,      // v4-v5-v0

							1, 1, 1,   0, 1, 1,   0, 1, 0,      // v0-v5-v6 (top)
							0, 1, 0,   1, 1, 0,   1, 1, 1,      // v6-v1-v0

							1, 1, 0,   0, 1, 0,   0, 0, 0,      // v1-v6-v7 (left)
							0, 0, 0,   1, 0, 0,   1, 1, 0,      // v7-v2-v1

							0, 0, 0,   0, 0, 1,   1, 0, 1,      // v7-v4-v3 (bottom)
							1, 0, 1,   1, 0, 0,   0, 0, 0,      // v3-v2-v7

							0, 0, 1,   0, 0, 0,   0, 1, 0,      // v4-v7-v6 (back)
							0, 1, 0,   0, 1, 1,   0, 0, 1 };    // v6-v5-v4

	// Generate vertex array object.
	GLuint VAO;
	glGenVertexArrays(1, &VAO);

	// bind the vertex array object first, then bind and set vertex buffers.
	glBindVertexArray(VAO);

	// enable our frame buffer.
	glEnable(GL_FRAMEBUFFER_SRGB);

	// Generate vertex buffer for position attribute.
	GLuint boxVertbuffer;
	glGenBuffers(1, &boxVertbuffer);

	// now set the viewport.
	glViewport(0, 0, res.w, res.h);

	// bind the vbuffer
	glBindBuffer(GL_ARRAY_BUFFER, boxVertbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(boxVerts), boxVerts, GL_STATIC_DRAW);

	// gernerate color buffer for color attribute.
	GLuint boxColorbuffer;
	glGenBuffers(1, &boxColorbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, boxColorbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(boxColors), boxColors, GL_STATIC_DRAW);

	// position attribute in shader
	GLuint loc1;
	loc1 = glGetAttribLocation(programID, "position");
	glBindBuffer(GL_ARRAY_BUFFER, boxVertbuffer);
	glVertexAttribPointer(
		loc1,               // match with shader.
		3,                  // size
		GL_FLOAT,           // type
		GL_FALSE,           // normalized?
		0,					// stride
		(void*)0			// array buffer offset
	);
	glEnableVertexAttribArray(loc1);

	// color attribute in shader.
	GLuint loc2;
	loc2 = glGetAttribLocation(programID, "color");
	glBindBuffer(GL_ARRAY_BUFFER, boxColorbuffer);
	glVertexAttribPointer(
		loc2,								// must match the layout in the shader.
		3,									// size
		GL_FLOAT,							// type
		GL_FALSE,							// normalized?
		0,									// stride
		(void*)0							// array buffer offset
	);
	glEnableVertexAttribArray(loc2);
	BOOL bRet;

	// main loop
	while (1)
	{
		// handle window messages.
		bRet = GetMessage(&msg, NULL, 0, 0);
		if (bRet > 0)
		{
<<<<<<< HEAD
			if (WM_QUIT == msg.message) {
				break;
			}
			else {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
=======
			TranslateMessage(&msg);
			DispatchMessage(&msg);
>>>>>>> parent of de35715 (implement render loop)
		}
		else
		{
			break;
		}

		// clear our screen
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// use depth test!!
		//glEnable(GL_DEPTH_TEST);
		//glDepthFunc(GL_LESS);

		// send our matrix info to the shader.
		glUniformMatrix4fv(modelID, 1, GL_TRUE, model.get());
		glUniformMatrix4fv(viewID, 1, GL_FALSE, view.get());
		glUniformMatrix4fv(projID, 1, GL_TRUE, proj.get());

		// use the shader
		glUseProgram(programID);

		// DRAW HERE PLEASE!!!!!!
		glDrawArrays(GL_TRIANGLES, 0, 36); // 

		// swap the window buffers.
		SwapBuffers(winState.appDC);
	}

	// Cleanup VBO and shader
	glDeleteBuffers(1, &boxVertbuffer);
	glDeleteBuffers(1, &boxColorbuffer);
	glDeleteProgram(programID);
	glDeleteVertexArrays(1, &VAO);

	// clean up windows.
	sgQueueEvents = false;
	wglDeleteContext((HGLRC)mContext);
	ReleaseDC(window, winState.appDC);
	DestroyWindow(window);
}
