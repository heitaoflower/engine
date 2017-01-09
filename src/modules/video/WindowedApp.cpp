/**
 * @file
 */

#include "WindowedApp.h"
#include "core/Common.h"
#include "core/Singleton.h"
#include "core/Var.h"
#include "GLFunc.h"
#include "Shader.h"
#include "core/Color.h"
#include "core/command/Command.h"
#include "GLVersion.h"
#include "core/Singleton.h"
#include "ShaderManager.h"
#include "util/KeybindingHandler.h"
#include <nfd.h>

namespace video {

namespace {
inline void checkError(const char *file, unsigned int line, const char *function) {
	const char *error = SDL_GetError();
	if (*error != '\0') {
		Log::error("%s (%s:%i => %s)", error, file, line, function);
		SDL_ClearError();
	} else {
		Log::error("unknown error (%s:%i => %s)", file, line, function);
	}
}
#define sdlCheckError() checkError(__FILE__, __LINE__, __PRETTY_FUNCTION__)
}

WindowedApp::ProfilerGPU::ProfilerGPU(const std::string& name, uint16_t maxSamples) :
		_name(name), _maxSampleCount(maxSamples) {
	core_assert(maxSamples > 0);
	_samples.reserve(_maxSampleCount);
}

WindowedApp::ProfilerGPU::~ProfilerGPU() {
	core_assert_msg(_id == 0u, "Forgot to shutdown gpu profiler: %s", _name.c_str());
	shutdown();
}

const std::vector<double>& WindowedApp::ProfilerGPU::samples() const {
	return _samples;
}

bool WindowedApp::ProfilerGPU::init() {
	glGenQueries(1, &_id);
	return _id != 0u;
}

void WindowedApp::ProfilerGPU::shutdown() {
	if (_id != 0u) {
		glDeleteQueries(1, &_id);
		_id = 0u;
	}
}

void WindowedApp::ProfilerGPU::enter() {
	if (_id == 0u) {
		return;
	}
	core_assert(_state == 0 || _state == 2);

	if (_state == 0) {
		glBeginQuery(GL_TIME_ELAPSED, _id);
		_state = 1;
	}
}

void WindowedApp::ProfilerGPU::leave() {
	if (_id == 0u) {
		return;
	}
	core_assert(_state == 1 || _state == 2);

	if (_state == 1) {
		glEndQuery(GL_TIME_ELAPSED);
		_state = 2;
	} else if (_state == 2) {
		GLint availableResults = 0;
		glGetQueryObjectiv(_id, GL_QUERY_RESULT_AVAILABLE, &availableResults);
		if (availableResults > 0) {
			_state = 0;
			GLuint64 time = 0;
			glGetQueryObjectui64v(_id, GL_QUERY_RESULT, &time);
			const double timed = double(time);
			_samples[_sampleCount & (_maxSampleCount - 1)] = timed;
			++_sampleCount;
			_max = std::max(_max, timed);
			_min = std::min(_min, timed);
			_avg = _avg * 0.5 + timed / 1e9 * 0.5;
		}
	}
}

const std::string& WindowedApp::ProfilerGPU::name() const {
	return _name;
}

double WindowedApp::ProfilerGPU::avg() const {
	return _avg;
}

double WindowedApp::ProfilerGPU::minimum() const {
	return _min;
}

double WindowedApp::ProfilerGPU::maximum() const {
	return _max;
}

WindowedApp::WindowedApp(const io::FilesystemPtr& filesystem, const core::EventBusPtr& eventBus, const core::TimeProviderPtr& timeProvider, uint16_t traceport) :
		App(filesystem, eventBus, timeProvider, traceport), _window(nullptr), _glcontext(nullptr), _dimension(-1), _aspect(1.0f) {
}

void WindowedApp::setupLimits() {
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &_state.limits[std::enum_value(Limit::MaxTextureSize)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &_state.limits[std::enum_value(Limit::MaxCubeMapTextureSize)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, &_state.limits[std::enum_value(Limit::MaxViewPortWidth)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &_state.limits[std::enum_value(Limit::MaxDrawBuffers)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &_state.limits[std::enum_value(Limit::MaxVertexAttribs)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &_state.limits[std::enum_value(Limit::MaxCombinedTextureImageUnits)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &_state.limits[std::enum_value(Limit::MaxVertexTextureImageUnits)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_ELEMENTS_INDICES, &_state.limits[std::enum_value(Limit::MaxElementIndices)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &_state.limits[std::enum_value(Limit::MaxElementVertices)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_FRAGMENT_INPUT_COMPONENTS, &_state.limits[std::enum_value(Limit::MaxFragmentInputComponents)]);
	GL_checkError();
#ifdef GL_MAX_VERTEX_UNIFORM_VECTORS
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &_state.limits[std::enum_value(Limit::MaxVertexUniformComponents)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &_state.limits[std::enum_value(Limit::MaxFragmentUniformComponents)]);
	GL_checkError();
#else
	GL_checkError();
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &_state.limits[std::enum_value(Limit::MaxVertexUniformComponents)]);
	GL_checkError();
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &_state.limits[std::enum_value(Limit::MaxFragmentUniformComponents)]);
#endif
	GL_checkError();
	Log::info("GL_MAX_ELEMENTS_VERTICES: %i", _state.limits[std::enum_value(Limit::MaxElementVertices)]);
	Log::info("GL_MAX_ELEMENTS_INDICES: %i", _state.limits[std::enum_value(Limit::MaxElementIndices)]);
}

void WindowedApp::setupFeatures() {
	const std::vector<const char *> array[] = {
		{"_texture_compression_s3tc", "_compressed_texture_s3tc", "_texture_compression_dxt1"},
		{"_texture_compression_pvrtc", "_compressed_texture_pvrtc"},
		{},
		{"_compressed_ATC_texture", "_compressed_texture_atc"},
		{"_texture_float"},
		{"_texture_half_float"},
		{"_instanced_arrays"},
		{"_debug_output"}
	};

	Log::info("Opengl feature detection:");
	for (size_t i = 0; i < SDL_arraysize(array); ++i) {
		const std::vector<const char *>& a = array[i];
		for (const char *s : a) {
			_state.features[i] = SDL_GL_ExtensionSupported(s);
			if (_state.features[i]) {
				break;
			}
			++s;
		}
	}

	int mask = 0;
	if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &mask) != -1) {
		if ((mask & SDL_GL_CONTEXT_PROFILE_CORE) != 0) {
			_state.features[std::enum_value(Feature::TextureCompressionDXT)] = true;
			_state.features[std::enum_value(Feature::InstancedArrays)] = true;
			_state.features[std::enum_value(Feature::TextureFloat)] = true;
		}
	}

#if SDL_VIDEO_OPENGL_ES2
	_state.features[std::enum_value(Feature::TextureHalfFloat)] = SDL_GL_ExtensionSupported("_texture_half_float");
#else
	_state.features[std::enum_value(Feature::TextureHalfFloat)] = _state.features[std::enum_value(Feature::TextureFloat)];
#endif

#if SDL_VIDEO_OPENGL_ES3
	_state.features[std::enum_value(Feature::InstancedArrays)] = true;
	_state.features[std::enum_value(Feature::TextureCompressionETC2)] = true;
#endif

	if (!_state.features[std::enum_value(Feature::InstancedArrays)]) {
		Log::warn("instanced_arrays extension not found!");
	}
}

void WindowedApp::onAfterRunning() {
	core_trace_scoped(WindowedAppAfterRunning);
	SDL_GL_SwapWindow(_window);
}

core::AppState WindowedApp::onRunning() {
	core::App::onRunning();

	for (KeyMapConstIter it = _keys.begin(); it != _keys.end(); ++it) {
		const int key = it->first;
		auto range = _bindings.equal_range(key);
		for (auto i = range.first; i != range.second; ++i) {
			const std::string& command = i->second.first;
			const int16_t modifier = i->second.second;
			if (it->second == modifier && command[0] == '+') {
				core_assert_always(1 == core::Command::execute(command + " true"));
				_keys[key] = modifier;
			}
		}
	}

	core_trace_scoped(WindowedAppOnRunning);
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_QUIT:
			return core::AppState::Cleanup;
		default: {
			core_trace_scoped(WindowedAppEventHandler);
			const bool running = core::Singleton<io::EventHandler>::getInstance().handleEvent(event);
			if (!running) {
				return core::AppState::Cleanup;
			}
			break;
		}
		}
	}

	core_trace_gl_scoped(WindowedAppPrepareContext);
	SDL_GL_MakeCurrent(_window, _glcontext);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	// TODO: maybe only do this every nth frame?
	core::Singleton<ShaderManager>::getInstance().update();

	return core::AppState::Running;
}

void WindowedApp::onWindowResize() {
	int _width, _height;
	SDL_GetWindowSize(_window, &_width, &_height);
	_aspect = _width / static_cast<float>(_height);
	_dimension = glm::ivec2(_width, _height);
	glViewport(0, 0, _width, _height);
}

bool WindowedApp::onKeyRelease(int32_t key) {
	auto range = _bindings.equal_range(key);
	for (auto i = range.first; i != range.second; ++i) {
		const std::string& command = i->second.first;
		if (command[0] == '+' && _keys.erase(key) > 0) {
			core_assert_always(1 == core::Command::execute(command + " false"));
		}
	}
	return true;
}

bool WindowedApp::onKeyPress(int32_t key, int16_t modifier) {
	if (modifier & KMOD_LALT) {
		if (key == SDLK_RETURN) {
			const int flags = SDL_GetWindowFlags(_window);
			if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
				SDL_SetWindowFullscreen(_window, 0);
			} else {
				SDL_SetWindowFullscreen(_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
			}
			return true;
		}
	}

	if (util::executeCommandsForBinding(_keys, _bindings, key, modifier)) {
		return true;
	}

	return false;
}

bool WindowedApp::loadKeyBindings(const std::string& filename) {
	const std::string& bindings = filesystem()->load(filename);
	if (bindings.empty()) {
		return false;
	}
	const util::KeybindingParser p(bindings);
	_bindings.insert(p.getBindings().begin(), p.getBindings().end());
	return true;
}

core::AppState WindowedApp::onInit() {
	core::AppState state = App::onInit();
	if (state != core::AppState::Running) {
		return state;
	}

	if (SDL_Init(SDL_INIT_VIDEO) == -1) {
		sdlCheckError();
		return core::AppState::Cleanup;
	}

	SDL_StopTextInput();

	if (!loadKeyBindings()) {
		Log::error("failed to init the global keybindings");
	}
	loadKeyBindings(_appname + "-keybindings.cfg");

	core::Singleton<io::EventHandler>::getInstance().registerObserver(this);

	SDL_DisplayMode displayMode;
	const int numDisplays = std::max(0, SDL_GetNumVideoDisplays());
	const int displayIndex = glm::clamp(core::Var::getSafe(cfg::ClientWindowDisplay)->intVal(), 0, numDisplays);
	SDL_GetDesktopDisplayMode(displayIndex, &displayMode);

	for (int i = 0; i < numDisplays; ++i) {
		SDL_Rect dr;
		if (SDL_GetDisplayBounds(i, &dr) == -1) {
			continue;
		}
		Log::info("Display %i: %i:%i x %i:%i", i, dr.x, dr.y, dr.w, dr.h);
	}

	int width = core::Var::get(cfg::ClientWindowWidth, displayMode.w)->intVal();
	int height = core::Var::get(cfg::ClientWindowHeight, displayMode.h)->intVal();

	const core::VarPtr& glVersion = core::Var::getSafe(cfg::ClientOpenGLVersion);
	int glMinor = 0, glMajor = 0;
	if (std::sscanf(glVersion->strVal().c_str(), "%3i.%3i", &glMajor, &glMinor) != 2) {
		glMajor = 3;
		glMinor = 0;
	}
	GLVersion glv(glMajor, glMinor);
	for (size_t i = 0; i < SDL_arraysize(GLVersions); ++i) {
		if (GLVersions[i].version == glv) {
			Shader::glslVersion = GLVersions[i].glslVersion;
		}
	}
	SDL_ClearError();
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	const core::VarPtr& multisampleBuffers = core::Var::getSafe(cfg::ClientMultiSampleBuffers);
	const core::VarPtr& multisampleSamples = core::Var::getSafe(cfg::ClientMultiSampleSamples);

	bool multisampling = multisampleSamples->intVal() > 0 && multisampleBuffers->intVal() > 0;
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, multisampleBuffers->intVal());
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisampleSamples->intVal());
#ifdef DEBUG
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, glv.majorVersion);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glv.minorVersion);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

	const bool fullscreen = core::Var::getSafe(cfg::ClientFullscreen)->boolVal();

	int flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
	if (fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
	}

	const int videoDrivers = SDL_GetNumVideoDrivers();
	for (int i = 0; i < videoDrivers; ++i) {
		Log::info("available driver: %s", SDL_GetVideoDriver(i));
	}

	SDL_Rect displayBounds;
	SDL_GetDisplayBounds(displayIndex, &displayBounds);
	Log::info("driver: %s", SDL_GetCurrentVideoDriver());
	Log::info("found %i displays (use %i at %i:%i)", numDisplays, displayIndex, displayBounds.x, displayBounds.y);
	if (fullscreen && numDisplays > 1) {
		width = displayMode.w;
		height = displayMode.h;
		Log::info("use fake fullscreen for display %i: %i:%i", displayIndex, width, height);
	}

	_window = SDL_CreateWindow(_appname.c_str(), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), width, height, flags);
	if (!_window) {
		sdlCheckError();
		return core::AppState::Cleanup;
	}

	_glcontext = SDL_GL_CreateContext(_window);

	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &glv.majorVersion);
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &glv.minorVersion);
	Log::info("got gl context: %i.%i", glv.majorVersion, glv.minorVersion);

	const bool vsync = core::Var::getSafe(cfg::ClientVSync)->boolVal();
	if (vsync) {
		if (SDL_GL_SetSwapInterval(-1) == -1) {
			if (SDL_GL_SetSwapInterval(1) == -1) {
				Log::warn("Could not activate vsync: %s", SDL_GetError());
			}
		}
	} else {
		SDL_GL_SetSwapInterval(0);
	}
	if (SDL_GL_GetSwapInterval() == 0) {
		Log::info("Deactivated vsync");
	} else {
		Log::info("Activated vsync");
	}

	int buffers, samples;
	SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &buffers);
	SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &samples);
	if (buffers == 0 || samples == 0) {
		Log::warn("Could not get FSAA context");
		multisampling = false;
	} else {
		Log::info("Got FSAA context with %i buffers and %i samples", buffers, samples);
	}

	SDL_DisableScreenSaver();

	if (SDL_SetWindowBrightness(_window, 1.0f) == -1) {
		sdlCheckError();
	}

	const bool grabMouse = false;
	if (grabMouse && (!fullscreen || numDisplays > 1)) {
		SDL_SetWindowGrab(_window, SDL_TRUE);
	}

	int screen = 0;
	int modes = SDL_GetNumDisplayModes(screen);
	Log::info("possible display modes:");
	for (int i = 0; i < modes; i++) {
		SDL_GetDisplayMode(screen, i, &displayMode);
		const char *name = SDL_GetPixelFormatName(displayMode.format);
		Log::info("%ix%i@%iHz %s", displayMode.w, displayMode.h, displayMode.refresh_rate, name);
	}

	// some platforms may override or hardcode the resolution - so
	// we have to query it here to get the actual resolution
	int _width, _height;
	SDL_GetWindowSize(_window, &_width, &_height);
	_dimension = glm::ivec2(_width, _height);
	_aspect = _width / static_cast<float>(_height);

	GLLoadFunctions();
	glViewport(0, 0, _width, _height);

	setupLimits();
	setupFeatures();

	int numExts;
	glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
	Log::info("OpenGL extensions:");
	for (int i = 0; i < numExts; i++) {
		const char *extensionStr = (const char *) glGetStringi(GL_EXTENSIONS, i);
		Log::info("%s", extensionStr);
	}

	if (multisampling) {
		glEnable(GL_MULTISAMPLE);
	}

	core_trace_gl_init();

	return state;
}

core::AppState WindowedApp::onConstruct() {
	core::AppState state = App::onConstruct();
	core::Var::get(cfg::ClientMultiSampleBuffers, "1");
	core::Var::get(cfg::ClientMultiSampleSamples, "4");
	core::Var::get(cfg::ClientFullscreen, "true");
	core::Var::get(cfg::ClientShadowMap, "true", core::CV_SHADER);
	core::Var::get(cfg::ClientDebugShadowMapCascade, "false", core::CV_SHADER);
	core::Var::get(cfg::ClientGamma, "2.2", core::CV_SHADER);
	core::Var::get(cfg::ClientWindowDisplay, 0);
	core::Var::get(cfg::ClientOpenGLVersion, "3.1", core::CV_READONLY);
#ifdef DEBUG
	const char *defaultSyncValue = "false";
#else
	const char *defaultSyncValue = "true";
#endif
	core::Var::get(cfg::ClientVSync, defaultSyncValue);

	return state;
}

core::AppState WindowedApp::onCleanup() {
	core::Singleton<io::EventHandler>::getInstance().removeObserver(this);
	SDL_GL_DeleteContext(_glcontext);
	SDL_DestroyWindow(_window);
	SDL_Quit();
	core_trace_gl_shutdown();

	std::string keybindings;

	for (util::BindMap::const_iterator i = _bindings.begin(); i != _bindings.end(); ++i) {
		const int32_t key = i->first;
		const util::CommandModifierPair& pair = i->second;
		const std::string keyName = core::string::toLower(SDL_GetKeyName(key));
		const int16_t modifier = pair.second;
		std::string modifierKey;
		if (modifier & KMOD_ALT) {
			modifierKey += "alt+";
		}
		if (modifier & KMOD_SHIFT) {
			modifierKey += "shift+";
		}
		if (modifier & KMOD_CTRL) {
			modifierKey += "ctrl+";
		}
		const std::string& command = pair.first;
		keybindings += modifierKey + keyName + " " + command + '\n';
	}
	Log::info("%s", keybindings.c_str());
	filesystem()->write("keybindings.cfg", keybindings);

	return App::onCleanup();
}

std::string WindowedApp::fileDialog(OpenFileMode mode, const std::string& filter) {
	nfdchar_t *outPath = nullptr;
	nfdresult_t result;
	if (mode == OpenFileMode::Open) {
		result = NFD_OpenDialog(filter.c_str(), nullptr, &outPath);
	} else if (mode == OpenFileMode::Save) {
		result = NFD_SaveDialog(filter.c_str(), nullptr, &outPath);
	} else {
		result = NFD_PickFolder(nullptr, &outPath);
	}
	if (outPath && result == NFD_OKAY) {
		Log::info("Selected %s", outPath);
		std::string path = outPath;
		free(outPath);
		SDL_RaiseWindow(_window);
		SDL_SetWindowInputFocus(_window);
		return path;
	} else if (result == NFD_CANCEL) {
		Log::info("Cancel selection");
	} else if (result == NFD_ERROR) {
		const char *error = NFD_GetError();
		if (error == nullptr) {
			error = "Unknown";
		}
		Log::info("Error: %s", error);
	}
	free(outPath);
	SDL_RaiseWindow(_window);
	SDL_SetWindowInputFocus(_window);
	return "";
}

}
