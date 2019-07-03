/**
 * @file
 */

#include "VoxEdit.h"
#include "core/Color.h"
#include "voxel/MaterialColor.h"
#include "core/command/Command.h"
#include "core/command/CommandCompleter.h"
#include "video/Renderer.h"
#include "ui/VoxEditWindow.h"
#include "io/Filesystem.h"

VoxEdit::VoxEdit(const metric::MetricPtr& metric, const io::FilesystemPtr& filesystem, const core::EventBusPtr& eventBus, const core::TimeProviderPtr& timeProvider, const video::MeshPoolPtr& meshPool) :
		Super(metric, filesystem, eventBus, timeProvider), _mainWindow(nullptr), _meshPool(meshPool), _sceneMgr(voxedit::sceneMgr()) {
	init(ORGANISATION, "voxedit");
	_allowRelativeMouseMode = false;
}

bool VoxEdit::importheightmapFile(const std::string& file) {
	return _mainWindow->importHeightmap(file);
}

bool VoxEdit::importplaneFile(const std::string& file) {
	return _mainWindow->importAsPlane(file);
}

bool VoxEdit::importpaletteFile(const std::string& file) {
	return _mainWindow->importPalette(file);
}

bool VoxEdit::saveFile(const std::string& file) {
	return _mainWindow->save(file);
}

bool VoxEdit::screenshotFile(const std::string& file) {
	return _mainWindow->saveScreenshot(file);
}

bool VoxEdit::loadFile(const std::string& file) {
	return _mainWindow->load(file);
}

bool VoxEdit::prefabFile(const std::string& file) {
	return _mainWindow->prefab(file);
}

bool VoxEdit::importFile(const std::string& file) {
	return _mainWindow->importMesh(file);
}

bool VoxEdit::newFile(bool force) {
	return _mainWindow->createNew(force);
}

bool VoxEdit::exportFile(const std::string& file) {
	return _mainWindow->exportFile(file);
}

core::AppState VoxEdit::onCleanup() {
	const core::AppState state = Super::onCleanup();
	_meshPool->shutdown();
	_sceneMgr.shutdown();
	return state;
}

void VoxEdit::onDropFile(const std::string& file) {
	_sceneMgr.load(file);
}

core::AppState VoxEdit::onConstruct() {
	const core::AppState state = Super::onConstruct();

	_framesPerSecondsCap->setVal(60.0f);

	_sceneMgr.construct();

#define COMMAND_FILE(command, help) \
	core::Command::registerCommand(#command, [this] (const core::CmdArgs& args) { \
		const std::string file = args.empty() ? "" : args[0]; \
		if (!command##File(file)) { \
			Log::error("Failed to execute '" #command "' for file %s", file.c_str()); \
		} \
	}).setArgumentCompleter(core::fileCompleter(_lastDirectory)).setHelp(help)

	COMMAND_FILE(screenshot, "Save the current viewport as screenshot");
	COMMAND_FILE(save, "Save the current state to the given file");
	COMMAND_FILE(export, "Export the current state to the given file");
	COMMAND_FILE(load, "Load a scene from the given file");
	COMMAND_FILE(prefab, "Add a model to the existing scene from the given file");
	COMMAND_FILE(import, "Import a mesh from the given file and tries to voxelize it");
	COMMAND_FILE(importheightmap, "Import a heightmap into the volume");
	COMMAND_FILE(importplane, "Import an image as a plane");
	COMMAND_FILE(importpalette, "Import an image as a palette");
#undef COMMAND_FILE

	core::Command::registerCommand("new",
			[this] (const core::CmdArgs& args) {newFile();}).setHelp(
			"Create a new scene");
	core::Command::registerCommand("toggleviewport",
			[this] (const core::CmdArgs& args) {_mainWindow->toggleviewport();}).setHelp(
			"Toggle quad view on/off");
	core::Command::registerCommand("resetcamera",
			[this] (const core::CmdArgs& args) {_mainWindow->resetCamera();}).setHelp(
			"Reset cameras");
	core::Command::registerCommand("dialog_noise",
			[this] (const core::CmdArgs& args) {
				tb::TBWidgetEvent event(tb::EVENT_TYPE_CUSTOM);
				event.ref_id = tb::TBGetHash("dialog_noise");
				_mainWindow->invokeEvent(event);
			}).setHelp("Opens the noise dialog");

	core::Var::get(cfg::VoxEditLastPalette, "nippon");

	return state;
}

core::AppState VoxEdit::onInit() {
	const core::AppState state = Super::onInit();
	if (state != core::AppState::Running) {
		return state;
	}

	const char *paletteName = core::Var::getSafe(cfg::VoxEditLastPalette)->strVal().c_str();
	const io::FilesystemPtr& filesystem = core::App::getInstance()->filesystem();
	const io::FilePtr& paletteFile = filesystem->open(core::string::format("palette-%s.png", paletteName));
	const io::FilePtr& luaFile = filesystem->open(core::string::format("palette-%s.lua", paletteName));
	if (!voxel::initMaterialColors(paletteFile, luaFile)) {
		Log::warn("Failed to initialize the palette data for %s, falling back to default", paletteName);
		if (!voxel::initDefaultMaterialColors()) {
			Log::error("Failed to initialize the palette data");
			return core::AppState::InitFailure;
		}
	}

	if (!_meshPool->init()) {
		Log::error("Failed to initialize the mesh pool");
		return core::AppState::InitFailure;
	}

	if (!_sceneMgr.init()) {
		Log::error("Failed to initialize the scene manager");
		return core::AppState::InitFailure;
	}

	_mainWindow = new voxedit::VoxEditWindow(this);
	if (!_mainWindow->init()) {
		Log::error("Failed to initialize the main window");
		return core::AppState::InitFailure;
	}

	video::clearColor(::core::Color::Black);
	video::enable(video::State::DepthTest);
	video::depthFunc(video::CompareFunc::LessEqual);
	video::enable(video::State::CullFace);
	video::enable(video::State::DepthMask);
	video::enable(video::State::Blend);
	video::blendFunc(video::BlendMode::SourceAlpha, video::BlendMode::OneMinusSourceAlpha);

	setRelativeMouseMode(false);

	return state;
}

core::AppState VoxEdit::onRunning() {
	core::AppState state = Super::onRunning();
	if (state != core::AppState::Running) {
		return state;
	}
	_sceneMgr.update(_now);
	_mainWindow->update();
	return state;
}

int main(int argc, char *argv[]) {
	const core::EventBusPtr& eventBus = std::make_shared<core::EventBus>();
	const io::FilesystemPtr& filesystem = std::make_shared<io::Filesystem>();
	const core::TimeProviderPtr& timeProvider = std::make_shared<core::TimeProvider>();
	const video::MeshPoolPtr& meshPool = std::make_shared<video::MeshPool>();
	const metric::MetricPtr& metric = std::make_shared<metric::Metric>();
	VoxEdit app(metric, filesystem, eventBus, timeProvider, meshPool);
	return app.startMainLoop(argc, argv);
}
