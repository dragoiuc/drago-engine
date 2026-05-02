#include <iostream>
#include <algorithm>
#include <filesystem>
#include <vector>

#include "Engine.h"
#include "AudioDB.h"
#include "GameEditor.h"
#include "Helper.h"
#include "ImGuiLayer.h"
#include "ImageDB.h"
#include "Input.h"
#include "ResourcePaths.h"
#include "Renderer.h"
#include "TextDB.h"
#include <box2d/box2d.h>
#include <imgui.h>

namespace fs = std::filesystem;

namespace {
	enum class LaunchMode {
		Play,
		Edit
	};

	enum class SessionResult {
		QuitApplication,
		BackToLauncher
	};

	struct GameLaunchOption {
		std::string display_name = "";
		std::string description = "";
		fs::path game_root;
	};

	std::optional<fs::path> ResolveRunnableGameRoot(const fs::path& candidate) {
		if (!fs::is_directory(candidate)) {
			return std::nullopt;
		}

		if (fs::exists(candidate / fs::path("game.config"))) {
			return candidate;
		}

		const fs::path nestedResources = candidate / fs::path("resources");
		if (fs::is_directory(nestedResources) &&
			fs::exists(nestedResources / fs::path("game.config"))) {
			return nestedResources;
		}

		return std::nullopt;
	}

	std::string BuildRelativePathString(const fs::path& path) {
		std::error_code error;
		const fs::path relativePath = fs::relative(path, fs::current_path(), error);
		if (!error && !relativePath.empty()) {
			return relativePath.generic_string();
		}

		return path.generic_string();
	}

	std::string BuildGameDisplayName(const fs::path& gamesDirectory, const fs::path& gameRoot) {
		if (gameRoot == gamesDirectory) {
			return "Default Game";
		}

		const std::string folderName = gameRoot.filename().string();
		return folderName.empty() ? BuildRelativePathString(gameRoot) : folderName;
	}

	ImVec2 CalculateAvailableWindowSize(const ImVec2& margins) {
		const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
		return ImVec2(
			std::max(240.0f, displaySize.x - margins.x),
			std::max(180.0f, displaySize.y - margins.y));
	}

	ImVec2 ClampWindowSizeToAvailable(const ImVec2& desiredSize, const ImVec2& availableSize) {
		return ImVec2(
			std::min(desiredSize.x, availableSize.x),
			std::min(desiredSize.y, availableSize.y));
	}

	std::vector<GameLaunchOption> DiscoverLaunchOptions() {
		const fs::path gamesDirectory = ResourcePaths::GetDefaultGameLibraryRoot();
		std::vector<GameLaunchOption> options;

		const std::optional<fs::path> defaultGameRoot = ResolveRunnableGameRoot(gamesDirectory);
		if (defaultGameRoot.has_value() && *defaultGameRoot == gamesDirectory) {
			options.push_back(GameLaunchOption{
				BuildGameDisplayName(gamesDirectory, gamesDirectory),
				BuildRelativePathString(gamesDirectory),
				gamesDirectory
			});
		}

		if (!fs::exists(gamesDirectory) || !fs::is_directory(gamesDirectory)) {
			return options;
		}

		std::vector<GameLaunchOption> childOptions;
		for (const fs::directory_entry& entry : fs::directory_iterator(gamesDirectory)) {
			const std::optional<fs::path> gameRoot = ResolveRunnableGameRoot(entry.path());
			if (!gameRoot.has_value()) {
				continue;
			}

			childOptions.push_back(GameLaunchOption{
				entry.path().filename().string(),
				BuildRelativePathString(*gameRoot),
				*gameRoot
			});
		}

		std::sort(childOptions.begin(), childOptions.end(),
			[](const GameLaunchOption& lhs, const GameLaunchOption& rhs) {
				return lhs.display_name < rhs.display_name;
			});
		options.insert(options.end(), childOptions.begin(), childOptions.end());

		return options;
	}

	bool RunGameLauncher(
		Renderer& renderer,
		ImGuiLayer& imguiLayer,
		fs::path& selectedGameRoot,
		LaunchMode& launchMode) {
		const std::vector<GameLaunchOption> options = DiscoverLaunchOptions();
		int selectedIndex = options.empty() ? -1 : 0;
		bool launchRequested = false;
		launchMode = LaunchMode::Play;

		while (!launchRequested) {
			SDL_Event event;
			while (Helper::SDL_PollEvent(&event)) {
				imguiLayer.ProcessEvent(event);
				if (event.type == SDL_QUIT) {
					return false;
				}
			}

			renderer.BeginFrame(24, 26, 32);
			imguiLayer.BeginFrame();

			const ImVec2 availableSize = CalculateAvailableWindowSize(ImVec2(64.0f, 64.0f));
			const ImVec2 minimumSize =
				ClampWindowSizeToAvailable(ImVec2(560.0f, 340.0f), availableSize);
			const ImVec2 initialSize =
				ClampWindowSizeToAvailable(ImVec2(640.0f, 420.0f), availableSize);
			ImGui::SetNextWindowPos(ImVec2(32.0f, 32.0f), ImGuiCond_Appearing);
			ImGui::SetNextWindowSizeConstraints(minimumSize, availableSize);
			ImGui::SetNextWindowSize(initialSize, ImGuiCond_Appearing);

			if (ImGui::Begin(
				"Launch Game",
				nullptr,
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
				ImGui::TextWrapped("Choose a game folder to load. The launcher scans `./resources`, `./resources/<game>`, and `./resources/<game>/resources` for `game.config`.");
				ImGui::Separator();

				if (options.empty()) {
					ImGui::TextWrapped("No runnable game was found.");
					ImGui::Spacing();
					ImGui::TextUnformatted("Expected one of:");
					ImGui::BulletText("./resources/game.config");
					ImGui::BulletText("./resources/<game>/game.config");
					ImGui::BulletText("./resources/<game>/resources/game.config");
				}
				else {
					for (int i = 0; i < static_cast<int>(options.size()); ++i) {
						const GameLaunchOption& option = options[i];
						const std::string label = option.display_name + "##game_" + std::to_string(i);
						if (ImGui::Selectable(label.c_str(), selectedIndex == i)) {
							selectedIndex = i;
						}
						ImGui::TextDisabled("%s", option.description.c_str());
						if (i + 1 < static_cast<int>(options.size())) {
							ImGui::Spacing();
						}
					}
				}

				ImGui::Dummy(ImVec2(0.0f, 12.0f));
				if (selectedIndex >= 0 && selectedIndex < static_cast<int>(options.size())) {
					ImGui::Text("Selected: %s", options[selectedIndex].description.c_str());
				}

				const bool canLaunch = selectedIndex >= 0 && selectedIndex < static_cast<int>(options.size());
				if (!canLaunch) {
					ImGui::BeginDisabled();
				}
				if (ImGui::Button("Play")) {
					selectedGameRoot = options[selectedIndex].game_root;
					launchMode = LaunchMode::Play;
					launchRequested = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Edit")) {
					selectedGameRoot = options[selectedIndex].game_root;
					launchMode = LaunchMode::Edit;
					launchRequested = true;
				}
				if (!canLaunch) {
					ImGui::EndDisabled();
				}

				ImGui::SameLine();
				if (ImGui::Button("Quit")) {
					return false;
				}
			}
			ImGui::End();

			imguiLayer.Render(renderer.GetSDLRenderer());
			renderer.Present();
		}

		return true;
	}

	SessionResult RunGameSession(
		Renderer& renderer,
		ImGuiLayer& imguiLayer,
		const fs::path& selectedGameRoot) {
		ResourcePaths::SetGameRoot(selectedGameRoot);
		Input::Init();

		SessionResult result = SessionResult::QuitApplication;
		{
			Engine engine;
			engine._db.initDatabase();
			renderer.Initialize(engine._db);
			engine.LoadCurrentSceneComponents();
			if (engine._db.playerIndex >= 0 &&
				engine._db.playerIndex < static_cast<int>(engine._db.actors.size())) {
				engine.player = &engine._db.actors[engine._db.playerIndex];
			}

			AudioDB::Init();
			ImageDB::Init(renderer.GetSDLRenderer());
			ImageDB::LoadAll(engine._db.intro_images);
			if (!engine._db.game_over_bad_image.empty()) {
				ImageDB::LoadAll({ engine._db.game_over_bad_image });
			}
			if (!engine._db.game_over_good_image.empty()) {
				ImageDB::LoadAll({ engine._db.game_over_good_image });
			}
			if (engine.player != nullptr && !engine._db.hp_image.empty()) {
				ImageDB::LoadAll({ engine._db.hp_image });
			}
			engine.PrimeActorRenderCaches();
			TextDB::Init(renderer.GetSDLRenderer());
			TextDB::LoadConfiguredFont(engine._db);
			if (!engine._db.intro_bgm.empty()) {
				AudioDB::PlayChannel(0, engine._db.intro_bgm, -1);
				engine.introBgmWasPlaying = true;
			}

			bool windowRunning = true;
			while (windowRunning) {
				engine.ProcessInput(windowRunning, &imguiLayer);
				if (!windowRunning) {
					result = SessionResult::QuitApplication;
					break;
				}

				renderer.BeginFrame(engine._db);
				ImageDB::BeginFrame();
				TextDB::BeginFrame();
				imguiLayer.BeginFrame();
				Input::SetCaptureState(
					imguiLayer.WantsMouseCapture(),
					imguiLayer.WantsKeyboardCapture());

				engine.ProcessPendingSceneLoad();
				engine.RunPendingOnStarts();
				bool shouldPresent = true;
				if (engine.HasActiveIntro()) {
					engine.RenderIntro(renderer);
				}
				else {
					engine.UpdateGameplayActors();
					shouldPresent = engine.RenderGameplay(renderer);
				}

				const bool returnToLauncherRequested = imguiLayer.BuildDefaultOverlay(engine);
				if (returnToLauncherRequested) {
					result = SessionResult::BackToLauncher;
				}

				if (shouldPresent) {
					TextDB::RenderQueuedText();
					ImageDB::RenderQueuedPixels();
					imguiLayer.Render(renderer.GetSDLRenderer());
					renderer.Present();
				}

				Input::LateUpdate();

				if (returnToLauncherRequested) {
					break;
				}
			}
		}

		AudioDB::Shutdown();
		ImageDB::Shutdown();
		TextDB::Shutdown();
		Input::Init();
		return result;
	}
}

int main(int argc, char* argv[])
{
	std::ios::sync_with_stdio(false);
	std::cin.tie(nullptr);

	Renderer renderer;
	ImGuiLayer imguiLayer;
	renderer.Initialize("Game Launcher", 960, 540);
	imguiLayer.Initialize(renderer.GetSDLWindow(), renderer.GetSDLRenderer());

	bool applicationRunning = true;
	while (applicationRunning) {
		renderer.Initialize("Game Launcher", 960, 540);
		Input::Init();

		fs::path selectedGameRoot;
		LaunchMode launchMode = LaunchMode::Play;
		if (!RunGameLauncher(renderer, imguiLayer, selectedGameRoot, launchMode)) {
			break;
		}

		if (launchMode == LaunchMode::Edit) {
			GameEditor editor(selectedGameRoot);
			const GameEditor::Result editorResult = editor.Run(renderer, imguiLayer);
			if (editorResult == GameEditor::Result::QuitApplication) {
				applicationRunning = false;
			}
		}
		else {
			const SessionResult sessionResult =
				RunGameSession(renderer, imguiLayer, selectedGameRoot);
			if (sessionResult == SessionResult::QuitApplication) {
				applicationRunning = false;
			}
		}
	}
	return 0;
}
