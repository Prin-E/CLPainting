#include <Windows.h>
#include <GL/glew.h>
#include <GL/wglew.h>
#include <GL/GL.h>
#include <GL/GLU.h>
#include <CL/cl.h>
#include <CL/cl_gl.h>
#include <CL/cl_gl_ext.h>
#include <stdio.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND g_hWnd = NULL;
static HDC g_hDC = NULL;
static HGLRC g_hGLRC = NULL;

void InitGL();
void DrawGL();
void CleanGL();

static GLuint g_vertexBuffer;
static GLuint g_indexBuffer;
static GLuint g_uvBuffer;
static GLuint g_texture;
static GLuint g_sampler;

static GLuint g_vertexArray;
static GLuint g_shaderProgram;

static const int TEXTURE_SIZE_WIDTH = 2048;
static const int TEXTURE_SIZE_HEIGHT = 1024;
static const int BRUSH_SIZE = 64;

const char *vertexShader =
"#version 150\n"
"uniform float f;"
"in vec3 position;"
"in vec2 uv;"
"out vec2 out_uv;"
"void main() {"
"mat3 matrix;"
"matrix[0] = vec3(0.5 * cos(f), 0.5 * -sin(f), 0.0);"
"matrix[1] = vec3(0.5 * sin(f), 0.5 * cos(f), 0.0);"
"matrix[2] = vec3(0.0, 0.0, 1.0);"
//"vec3 pos = matrix * position;"
"vec3 pos = position;"
"gl_Position = vec4(pos.x, pos.y, pos.z, 1.0);"
"out_uv = uv;"
"}";

const char *fragmentShader =
"#version 150\n"
"precision highp float;"
"uniform sampler2D mainTex;"
"in vec2 out_uv;"
"out vec4 fragColor;"
"void main() {"
"vec4 texColor = texture(mainTex, out_uv);"
"fragColor = vec4(1.0, 1.0, 1.0, 1.0) * texColor;"
"}";

void InitCL();
void BeginDrawCL(int id);
void DrawCL(int id, float x, float y, int brushSize, unsigned int brushColor);
void EndDrawCL();
void CleanCL();

static float paintPrevX[16], paintPrevY[16];

static cl_platform_id g_clPlatform = 0;
static cl_device_id g_clDevice = 0;
static cl_context g_clContext = 0;
static cl_program g_clProgram = 0;
static cl_kernel g_clKernel = 0;
static cl_command_queue g_clCommandQueue = 0;
static cl_mem g_clGLImage = 0;

const char *g_clProgramSource =
"__constant sampler_t sampler = CLK_FILTER_NEAREST | CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE;"
"__kernel void paint(read_only image2d_t inImage, write_only image2d_t outImage, int px, int py, int ix, int iy, int brushSize, float4 brushColor) {"
"	int s = get_global_id(0);"
"	int t = get_global_id(1);"
"	int offsetX = abs(px - ix);"
"	int offsetY = abs(py - iy);"
"	int loop = min(64.0f, max(offsetX * 0.8f, offsetY * 0.8f));"
"	if(loop < 1) loop = 1;"
"	float4 color = read_imagef(inImage, sampler, (int2)(s,t));"

"	for(int i = 0; i <= loop; i++) {"
"		float lerp = (float)i/(float)loop;"
"		int cx = (int)((float)(px) * (1.0f - lerp) + (float)(ix) * lerp);"
"		int cy = (int)((float)(py) * (1.0f - lerp) + (float)(iy) * lerp);"
"		int lenSquare = (s - cx) * (s - cx) + (t - cy) * (t - cy);"
"		if(lenSquare <= brushSize * brushSize * 0.25f) {"
"			float alpha = 1.0f - ((float)lenSquare) / (brushSize * brushSize * 0.25f);"
"			float outAlpha = color.w * (1.0f - alpha) + alpha;"
"			color = (color * color.w * (float4)(1.0f - alpha, 1.0f - alpha, 1.0f - alpha, 1.0f - alpha) + (float4)(brushColor.x, brushColor.y, brushColor.z, alpha) * alpha) / outAlpha;"
"			color.w = outAlpha;"
"			write_imagef(outImage, (int2)(s,t), color);"
"		}"
"	}"

/*
"		int lenSquare = (s - ix) * (s - ix) + (t - iy) * (t - iy);"
"		if(lenSquare <= brushSize * brushSize / 4) {"
"			float alpha = 1.0f - ((float)lenSquare) / (brushSize * brushSize * 0.25f);"
"			float outAlpha = color.w * (1.0f - alpha) + alpha;"
"			color = (color * color.w * (float4)(1.0f - alpha, 1.0f - alpha, 1.0f - alpha, 1.0f - alpha) + (float4)(brushColor.x, brushColor.y, brushColor.z, alpha) * alpha) / outAlpha;"
"			color.w = outAlpha;"
"			write_imagef(outImage, (int2)(s,t), color);"
"		}"
*/

"}";

// float outAlpha = baseColor.a * (1.0f - blendColor.a) + blendColor.a;
// (baseColor.r * baseColor.a * (1.0f - blendColor.a) + blendColor.r * blendColor.a) * outAlphaDiv1, 

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	WNDCLASSEX wndClass;
	ZeroMemory(&wndClass, sizeof wndClass);

	wndClass.lpszClassName = TEXT("CL_Painting");
	wndClass.lpszMenuName = NULL;
	wndClass.lpfnWndProc = (WNDPROC)WndProc;
	wndClass.cbSize = sizeof wndClass;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hbrBackground = NULL;
	wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wndClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wndClass.hInstance = hInstance;

	RegisterClassEx(&wndClass);

	g_hWnd = CreateWindowEx(WS_EX_APPWINDOW, TEXT("CL_Painting"), TEXT("CL_Painting"), WS_OVERLAPPEDWINDOW, 40, 40, 1960, 980, NULL, NULL, hInstance, NULL);

	ShowWindow(g_hWnd, SW_SHOW);

	InitGL();
	InitCL();

	MSG msg; ZeroMemory(&msg, sizeof msg);
	BOOL loop = TRUE;

	LARGE_INTEGER frequency;
	LARGE_INTEGER prevTime, currentTime;
	int frame = 0;
	float delta1 = 0;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&prevTime);

	char windowTitle[256] = { 0, };

	while (loop) {
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT)
				loop = FALSE;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (loop) {
			QueryPerformanceCounter(&currentTime);
			DrawGL();

			frame += 1;
			delta1 += (double)(currentTime.QuadPart - prevTime.QuadPart) / (double)frequency.QuadPart;

			if (delta1 >= 1.0f) {
				delta1 -= 1.0f;

				sprintf_s(windowTitle, 256, "CL_Painting (Frame : %d)", frame);
				SetWindowTextA(g_hWnd, (LPCSTR)windowTitle);

				frame = 0;
			}

			prevTime = currentTime;
		}
	}

	return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static BOOL mouseDown = FALSE;
	static int windowWidth = 100, windowHeight = 100;

	switch (msg) {
	case WM_SIZE:
		windowWidth = (int)LOWORD(lParam);
		windowHeight = (int)HIWORD(lParam);
		glViewport(0, 0, windowWidth, windowHeight);
		return 0;
	case WM_LBUTTONDOWN:
		mouseDown = TRUE;
		for (int i = 0; i < 16; i++)
			BeginDrawCL(i);
		return 0;
	case WM_LBUTTONUP:
		mouseDown = FALSE;
		return 0;
	case WM_MOUSEMOVE:
	{
		if (mouseDown) {
			float x = LOWORD(lParam) / (float)windowWidth;
			float y = HIWORD(lParam) / (float)windowHeight;

			unsigned int color[6] = {
				0xff0000ff,
				0xff00ff00,
				0xffff0000,
				0xff00ffff,
				0xffffff00,
				0xff000000
			};
			/*
			for (int i = 0; i < 4; i++) {
				float xx = x + (i - 2) * 0.1f;
				for (int j = 0; j < 4; j++) {
					float yy = y + (j - 2) * 0.1f;
					DrawCL(i * 4 + j, xx, 1.0f - yy, BRUSH_SIZE, color[i]);
				}
			}
			*/
			DrawCL(0, x, 1.0f - y, BRUSH_SIZE, color[0]);
			EndDrawCL();
		}
	}
	return 0;
	case WM_DESTROY:
		CleanCL();
		CleanGL();
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void InitGL() {
	g_hDC = GetDC(g_hWnd);

	PIXELFORMATDESCRIPTOR pfd;
	ZeroMemory(&pfd, sizeof pfd);
	pfd.nSize = sizeof pfd;
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.iLayerType = PFD_MAIN_PLANE;
	pfd.cDepthBits = 16;
	pfd.cColorBits = 32;

	int pixelFormat = ChoosePixelFormat(g_hDC, &pfd);
	SetPixelFormat(g_hDC, pixelFormat, &pfd);

	HGLRC dummyRC = wglCreateContext(g_hDC);
	wglMakeCurrent(g_hDC, dummyRC);

	GLenum err = glewInit();
	if (err != GLEW_OK) {
		MessageBox(NULL, TEXT("OpenGL Error"), TEXT("Error"), MB_OK);
		PostQuitMessage(0);
		return;
	}

	if (GLEW_VERSION_3_2) {
		OutputDebugString(TEXT("OpenGL 3.2 Ready!"));

	}

	// OpenGL 3.2+
	GLint attribs[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
		WGL_CONTEXT_MINOR_VERSION_ARB, 2,
		WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		0
	};

	if (WGLEW_ARB_create_context && WGLEW_ARB_create_context_profile) {
		OutputDebugString(TEXT("WGLEW Profile Ready!"));
	}

	g_hGLRC = wglCreateContextAttribsARB(g_hDC, 0, attribs);

	wglMakeCurrent(0, 0);
	wglDeleteContext(dummyRC);
	wglMakeCurrent(g_hDC, g_hGLRC);

	if (WGLEW_EXT_swap_control) {
		wglSwapIntervalEXT(0);
	}

	// background color
	glClearColor(0.0f, 0.0f, 1.0f, 0.0f);

	// states
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);

	// shaders
	GLint shaderCompileResult = 0;
	GLint programLinkResult = 0;
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vertexShader, 0);
	glCompileShader(vs);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &shaderCompileResult);
	if (!shaderCompileResult) {
		char log[256] = { 0, };
		glGetShaderInfoLog(vs, 256, 0, log);
		MessageBoxA(g_hWnd, log, "GLSL vertex shader compile error", MB_OK | MB_ICONWARNING);
		OutputDebugStringA(log);
	}

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fragmentShader, 0);
	glCompileShader(fs);
	glGetShaderiv(fs, GL_COMPILE_STATUS, &shaderCompileResult);
	if (!shaderCompileResult) {
		char log[256] = { 0, };
		glGetShaderInfoLog(fs, 256, 0, log);
		MessageBoxA(g_hWnd, log, "GLSL fragment shader compile error", MB_OK | MB_ICONWARNING);
		OutputDebugStringA(log);
	}

	g_shaderProgram = glCreateProgram();
	glAttachShader(g_shaderProgram, vs);
	glAttachShader(g_shaderProgram, fs);
	glLinkProgram(g_shaderProgram);
	glGetProgramiv(g_shaderProgram, GL_LINK_STATUS, &programLinkResult);
	if (!programLinkResult) {
		char log[256] = { 0, };
		glGetProgramInfoLog(g_shaderProgram, 256, 0, log);
		MessageBoxA(g_hWnd, log, "GLSL fragment shader compile error", MB_OK | MB_ICONWARNING);
		OutputDebugStringA(log);
	}
	err = glGetError();

	GLbyte *texImage = (GLbyte*)malloc(sizeof(GLbyte) * TEXTURE_SIZE_WIDTH * TEXTURE_SIZE_HEIGHT * 4);
	memset(texImage, 0xff, sizeof(GLbyte) * TEXTURE_SIZE_WIDTH * TEXTURE_SIZE_HEIGHT * 4);

	glGenTextures(1, &g_texture);
	glBindTexture(GL_TEXTURE_2D, g_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURE_SIZE_WIDTH, TEXTURE_SIZE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, texImage);

	free(texImage);
	texImage = NULL;

	glGenSamplers(1, &g_sampler);
	glBindSampler(0, g_sampler);
	glSamplerParameteri(g_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(g_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(g_sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glSamplerParameteri(g_sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);

	err = glGetError();
	// vertex buffer objects
	float vertices[] = {
		-1.0f, -1.0f, 0.0f,
		1.0f, -1.0f, 0.0f,
		1.0f, 1.0f, 0.0f,
		-1.0f, 1.0f, 0.0f
	};

	int indices[] = {
		0, 1, 2, 0, 2, 3
	};

	float uvs[] = {
		0.0f, 0.0f,
		1.0f, 0.0f,
		1.0f, 1.0f,
		0.0f, 1.0f
	};

	err = glGetError();

	glGenBuffers(1, &g_vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, g_vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glGenBuffers(1, &g_indexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_indexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);
	glGenBuffers(1, &g_uvBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, g_uvBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);


	err = glGetError();

	glGenVertexArrays(1, &g_vertexArray);
	glBindVertexArray(g_vertexArray);
	glBindBuffer(GL_ARRAY_BUFFER, g_vertexBuffer);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, g_uvBuffer);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_indexBuffer);

	err = glGetError();

	glBindVertexArray(0);

	err = glGetError();
}


static float f = 0;

void DrawGL() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GLenum err = glGetError();


	glUseProgram(g_shaderProgram);
	glBindVertexArray(g_vertexArray);
	GLint fLoc = glGetAttribLocation(g_shaderProgram, "f");
	glUniform1f(fLoc, f);
	GLint samplerLoc = glGetUniformLocation(g_shaderProgram, "sampler");
	glUniform1i(samplerLoc, g_sampler);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_texture);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	err = glGetError();

	f += 0.016666f;

	if (f >= 6.28f)
		f -= 6.28f;

	SwapBuffers(g_hDC);
}

void CleanGL() {
	wglMakeCurrent(0, 0);

	glBindVertexArray(0);
	if (g_vertexArray) {
		glDeleteVertexArrays(1, &g_vertexArray);
		g_vertexArray = 0;
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	if (g_vertexBuffer) {
		glDeleteBuffers(1, &g_vertexBuffer);
		g_vertexBuffer = 0;
	}
	if (g_indexBuffer) {
		glDeleteBuffers(1, &g_indexBuffer);
		g_indexBuffer = 0;
	}

	glUseProgram(0);
	glDeleteProgram(g_shaderProgram);
}

void InitCL() {
	cl_uint platformCount = 0;
	cl_platform_id *platformIds = NULL;
	cl_int err = 0;

	clGetPlatformIDs(0, 0, &platformCount);
	if (platformCount == 0) {
		MessageBox(NULL, TEXT("This PC doesn't support OpenCL platform."), TEXT("OpenCL Error"), MB_OK | MB_ICONERROR);
		PostQuitMessage(0);
		return;
	}

	platformIds = (cl_platform_id *)malloc(sizeof(cl_platform_id) * platformCount);
	clGetPlatformIDs(1, platformIds, 0);

	for (cl_uint i = 0; i < platformCount; i++) {
		cl_platform_id platform = platformIds[i];
		cl_uint deviceCount = 0;

		cl_context_properties props[] = {
			CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
			CL_GL_CONTEXT_KHR, (cl_context_properties)g_hGLRC,
			CL_WGL_HDC_KHR, (cl_context_properties)g_hDC,
			0
		};

		clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, 0, &deviceCount);
		if (deviceCount > 0) {
			cl_device_id *deviceIds = (cl_device_id*)malloc(sizeof(cl_device_id)*deviceCount);
			memset(deviceIds, 0, sizeof(cl_uint)*deviceCount);

			clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, deviceCount, deviceIds, 0);
			for (cl_uint j = 0; j < deviceCount; j++) {
				err = CL_SUCCESS;

				cl_context context = clCreateContext(props, 1, &deviceIds[j], 0, 0, &err);
				if (err == CL_SUCCESS) {
					g_clPlatform = platform;
					g_clDevice = deviceIds[j];
					g_clContext = context;
					break;
				}
			}

			free(deviceIds);

			if (g_clContext != 0)
				break;
		}
	}

	if (g_clDevice == 0) {
		MessageBox(NULL, TEXT("Couldn't find OpenCL device."), TEXT("OpenCL Error"), MB_OK | MB_ICONERROR);
		PostQuitMessage(0);
		return;
	}

	cl_char vendorName[1024], deviceName[1024];

	memset(vendorName, 0, sizeof(cl_char) * 1024);
	memset(deviceName, 0, sizeof(cl_char) * 1024);

	clGetDeviceInfo(g_clDevice, CL_DEVICE_VENDOR, sizeof(vendorName),
		vendorName, 0);
	clGetDeviceInfo(g_clDevice, CL_DEVICE_NAME, sizeof(deviceName),
		deviceName, 0);

	OutputDebugStringA((LPCSTR)vendorName);
	OutputDebugStringA((LPCSTR)deviceName);

	g_clCommandQueue = clCreateCommandQueue(g_clContext, g_clDevice, 0, &err);

	g_clProgram = clCreateProgramWithSource(g_clContext, 1, (const char **)&g_clProgramSource, NULL, &err);
	err = clBuildProgram(g_clProgram, 0, 0, 0, 0, 0);
	if (err != CL_SUCCESS) {
		size_t len = 0;
		clGetProgramBuildInfo(g_clProgram, g_clDevice, CL_PROGRAM_BUILD_LOG, 0, 0, &len);
		if (len > 0) {
			char *log = (char *)malloc(len);
			clGetProgramBuildInfo(g_clProgram, g_clDevice, CL_PROGRAM_BUILD_LOG, len, log, 0);
			OutputDebugStringA(log);
			MessageBoxA(g_hWnd, log, "OpenCL build error", MB_OK | MB_ICONERROR);
			free(log);
			PostQuitMessage(0);
			return;
		}
	}

	g_clKernel = clCreateKernel(g_clProgram, "paint", &err);
	g_clGLImage = clCreateFromGLTexture(g_clContext, CL_MEM_READ_WRITE, GL_TEXTURE_2D, 0, g_texture, &err);
	err = clEnqueueAcquireGLObjects(g_clCommandQueue, 1, &g_clGLImage, 0, 0, 0);
}

void BeginDrawCL(int id) {
	paintPrevX[id] = -1.0f;
	paintPrevY[id] = -1.0f;
}

void DrawCL(int id, float x, float y, int brushSize, unsigned int brushColor) {
	if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) {
		BeginDrawCL(id);
		return;
	}

	cl_int err = CL_SUCCESS;

	if (paintPrevX[id] < 0.0f)
		paintPrevX[id] = x;
	if (paintPrevY[id] < 0.0f)
		paintPrevY[id] = y;

	int px = paintPrevX[id] * TEXTURE_SIZE_WIDTH;
	int py = paintPrevY[id] * TEXTURE_SIZE_HEIGHT;

	int ix = x * TEXTURE_SIZE_WIDTH;
	int iy = y * TEXTURE_SIZE_HEIGHT;

	if (px - ix > 256 || py - iy > 256)
		printf("Debug Me!");

	float color[4] = { ((unsigned char*)&brushColor)[0] / 255.0f, ((unsigned char*)&brushColor)[1] / 255.0f, ((unsigned char*)&brushColor)[2] / 255.0f, ((unsigned char*)&brushColor)[3] / 255.0f };

	err = clSetKernelArg(g_clKernel, 0, sizeof(cl_mem), &g_clGLImage);
	err = clSetKernelArg(g_clKernel, 1, sizeof(cl_mem), &g_clGLImage);
	err = clSetKernelArg(g_clKernel, 2, sizeof(cl_int), &px);
	err = clSetKernelArg(g_clKernel, 3, sizeof(cl_int), &py);
	err = clSetKernelArg(g_clKernel, 4, sizeof(cl_int), &ix);
	err = clSetKernelArg(g_clKernel, 5, sizeof(cl_int), &iy);
	err = clSetKernelArg(g_clKernel, 6, sizeof(cl_int), &brushSize);
	err = clSetKernelArg(g_clKernel, 7, sizeof(float) * 4, color);

	size_t global_work_offset[2] = { max(0, min(px, ix) - BRUSH_SIZE / 2), max(0, min(py, iy) - BRUSH_SIZE / 2) };
	size_t global_work_size[2] = { 2, 2 };
	size_t local_work_size[2] = { 2, 2 };


	while (global_work_size[0] < abs(px - ix) + BRUSH_SIZE) global_work_size[0] *= 2;
	while (global_work_size[1] < abs(py - iy) + BRUSH_SIZE) global_work_size[1] *= 2;

	if (global_work_offset[0] + global_work_size[0] >= TEXTURE_SIZE_WIDTH) {
		global_work_offset[0] = TEXTURE_SIZE_WIDTH - global_work_size[0];
	}
	if (global_work_offset[1] + global_work_size[1] >= TEXTURE_SIZE_HEIGHT) {
		global_work_offset[1] = TEXTURE_SIZE_HEIGHT - global_work_size[1];
	}

	local_work_size[0] = min(8, global_work_size[0] / 8);
	local_work_size[1] = min(8, global_work_size[1] / 8);

	if (global_work_size[0] > 0 && global_work_size[1] > 0)
		err = clEnqueueNDRangeKernel(g_clCommandQueue, g_clKernel, 2, global_work_offset, global_work_size, local_work_size, 0, 0, 0);

	paintPrevX[id] = x;
	paintPrevY[id] = y;
}

void EndDrawCL() {
	clFlush(g_clCommandQueue);
}

void CleanCL() {
	clReleaseMemObject(g_clGLImage);
	clReleaseCommandQueue(g_clCommandQueue);
	clReleaseKernel(g_clKernel);
	clReleaseContext(g_clContext);
}