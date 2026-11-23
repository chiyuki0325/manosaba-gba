DEBUG_MENU ?= 1
SAVE_BACKEND ?= flash

ifeq ($(BUTANO_BUILD),1)
include $(dir $(lastword $(MAKEFILE_LIST)))Butano.mk
else
.DEFAULT_GOAL := all
PYTHON ?= python3
BUTANO ?= vendor/butano/butano
TARGET ?= $(if $(filter 0,$(DEBUG_MENU)),manosaba-release,manosaba)$(if $(filter sram,$(SAVE_BACKEND)),-sram)
.PHONY: save-assets save-test
.PHONY: trial-screen-test
trial-screen-test: all
	../python_workspace/.venv/bin/python tools/run_trial_screen_test.py manosaba.gba build-butano/trial-screen-final
save-test: all
	../python_workspace/.venv/bin/python tools/run_save_test.py manosaba.gba build-butano/save-test

.PHONY: all story font nameplates story-ui choice-assets book-assets book-map-assets witch-book-vm-test witch-book-game-test witch-book-latency-test bad-end-test dialogue-history-test dialogue-color-test images reflow reflow-test dialogue-test stage-test stage-smoke choice-smoke actor-panel-test overlay-panel-test graphics-perf graphics-smoke audio-budget audio-perf graphics-import graphics-plan graphics-import-test graphics-link clean setup smoke story-test trial-resources trial-test trial-game-test trial-camera-test trial-presentation-test trial-walk trial-walk-chapters
all: prepared-assets music-cues
	$(MAKE) BUTANO_BUILD=1 BUTANO=$(abspath $(BUTANO))
	$(PYTHON) tools/rom_budget.py $(TARGET).gba

.PHONY: prepared-assets prepare-assets
prepared-assets:
	$(PYTHON) tools/check_prepared_assets.py

# Explicit raw-source conversion; ordinary ROM builds never enter these rules.
prepare-assets:
	$(MAKE) graphics-link
	$(MAKE) prepare-code
	$(PYTHON) tools/check_prepared_assets.py

.PHONY: prepare-code
prepare-code: warning-assets music-room-assets home-assets gallery-assets trial-map-assets execution-assets ending-assets sfx-assets nameplates story-ui toast-assets reenact-assets trial-resources choice-assets book-assets book-map-assets font story music-cues
	$(MAKE) save-assets


.PHONY: release release-menu-test

release-menu-test: release
	../python_workspace/.venv/bin/python tools/run_release_menu_test.py manosaba-release.gba build-release/menu-check

release:
	$(MAKE) DEBUG_MENU=0 all

.PHONY: warning-screen-test
warning-screen-test: all
	../python_workspace/.venv/bin/python tools/run_warning_screen_test.py manosaba.gba build-butano/warning-check

.PHONY: warning-assets
warning-assets: src/warning_screen_data.cpp src/warning_screen_data.h
src/warning_screen_data.cpp src/warning_screen_data.h &: tools/warning_screen_mockup.py tools/font.py assets/fusion-pixel-12px-monospaced-zh_hans.bdf assets/unifont-17.0.03.hex.gz
	../python_workspace/.venv/bin/python tools/warning_screen_mockup.py

.PHONY: home-assets
.PHONY: music-room-assets
music-room-assets: src/music_room_data.cpp assets/music-room/catalog.json
.PHONY: music-room-test
music-room-test: all
	../python_workspace/.venv/bin/python tools/run_music_room_test.py manosaba.gba build-butano/music-room-test
src/music_room_data.cpp assets/music-room/catalog.json &: tools/music_room_assets.py tools/music_room_mockups.py assets/music/room-titles.tsv assets/music/catalog.json assets/music/runtime.json ../manosaba-web/public/UI_Adv/Choice/ChoiceScreenBase.png
	../python_workspace/.venv/bin/python tools/music_room_assets.py

home-assets: src/home_art.cpp src/home_art.h src/home_bytes.s assets/home/home.bin
src/home_art.cpp src/home_art.h src/home_bytes.s assets/home/home.bin &: tools/home_assets.py tools/title_screen_mockups.py tools/start_menu_mockups.py tools/image_encoding.py tools/story.py tools/font.py tools/witch_book_icons.py ../manosaba-web/public/Home/Background/Background_1.png ../manosaba-web/public/Home/Background/Background_2.png ../manosaba-web/public/Home/TitleLogo@ZhHans.png $(wildcard ../manosaba-web/public/UI_Adv/Choice/*.png)
	OPENBLAS_NUM_THREADS=1 PYTHONPATH=tools:../python_workspace ../python_workspace/.venv/bin/python tools/home_assets.py

.PHONY: gallery-assets gallery-test
gallery-assets: src/gallery_data.cpp src/gallery_data.h assets/gallery/catalog.json
src/gallery_data.cpp src/gallery_data.h assets/gallery/catalog.json &: tools/gallery_assets.py assets/graphics-cache/import.json assets/graphics-cache/encoded.json assets/graphics-cache/linked/graphics.json
	OPENBLAS_NUM_THREADS=1 PYTHONPATH=tools:../python_workspace ../python_workspace/.venv/bin/python tools/gallery_assets.py
gallery-test: all
	../python_workspace/.venv/bin/python tools/run_gallery_test.py manosaba.gba build-butano/gallery-test

.PHONY: cg-regular-test
cg-regular-test: all
	../python_workspace/.venv/bin/python tools/run_cg_appearance_test.py manosaba.gba build-butano/cg-appearance-native
	OPENBLAS_NUM_THREADS=1 ../python_workspace/.venv/bin/python tools/cg_regular_assets_test.py
	../python_workspace/.venv/bin/python tools/run_gallery_test.py manosaba.gba build-butano/cg-regular-gallery

.PHONY: adv-instant-test
adv-instant-test: all
	../python_workspace/.venv/bin/python tools/run_adv_instant_test.py manosaba.gba build-butano/adv-instant

.PHONY: cg-detail-test
cg-detail-test:
	OPENBLAS_NUM_THREADS=1 ../python_workspace/.venv/bin/python tools/graphics_encoding_test.py
	OPENBLAS_NUM_THREADS=1 ../python_workspace/.venv/bin/python tools/cg_detail_validate.py

.PHONY: toast-assets
.PHONY: trial-map-assets
trial-map-assets: src/trial_map_data.cpp src/trial_map_bytes.s assets/trial-maps/pins.bin assets/trial-maps/catalog.json
src/trial_map_data.cpp src/trial_map_bytes.s assets/trial-maps/pins.bin assets/trial-maps/catalog.json &: tools/trial_map_assets.py tools/trial_cutins.py src/trial_map_data.h ../original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle
	PYTHONPATH=tools:../python_workspace ../python_workspace/.venv/bin/python tools/trial_map_assets.py
.PHONY: execution-assets execution-test
execution-test: all
	../python_workspace/.venv/bin/python tools/run_execution_test.py manosaba.gba build-butano/execution-test
execution-assets: src/execution_data.cpp
src/execution_data.cpp: tools/execution_assets.py tools/witch_book_icons.py tools/font.py src/execution_data.h assets/fusion-pixel-12px-monospaced-zh_hans.bdf $(wildcard ../manosaba-web/public/UI_ExecutionButton/*.png)
	../python_workspace/.venv/bin/python tools/execution_assets.py
.PHONY: ending-assets ending-test
ending-test: all
	../python_workspace/.venv/bin/python tools/run_ending_roll_test.py manosaba.gba build-butano/ending-roll-test
ending-assets: src/credits_data.cpp src/credits_bytes.s assets/ending-roll/text.bin assets/ending-roll/catalog.json src/ending_glyphs.txt
src/credits_data.cpp src/credits_bytes.s assets/ending-roll/text.bin assets/ending-roll/catalog.json src/ending_glyphs.txt &: tools/ending_roll_assets.py tools/ending_roll_research.py assets/ending-roll-titles-zh.json assets/source-metadata/trial.json assets/fusion-pixel-12px-monospaced-zh_hans.bdf ../original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-ui_assets_all.bundle
	PYTHONPATH=tools:../python_workspace ../python_workspace/.venv/bin/python tools/ending_roll_assets.py

.PHONY: reenact-assets
reenact-assets: src/reenact_data.cpp src/reenact_bytes.s assets/reenact/reenact.bin assets/reenact/catalog.json
src/reenact_data.cpp src/reenact_bytes.s assets/reenact/reenact.bin assets/reenact/catalog.json &: tools/reenact_assets.py tools/reenact_mockup.py tools/trial_cutins.py assets/reenact-layout.json src/reenact_data.h ../original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle
	PYTHONPATH=tools:../python_workspace ../python_workspace/.venv/bin/python tools/reenact_assets.py

toast-assets: src/toast_data.cpp
src/toast_data.cpp: tools/toast_assets.py tools/font.py tools/image_encoding.py assets/fusion-pixel-12px-monospaced-zh_hans.bdf
	../python_workspace/.venv/bin/python tools/toast_assets.py

save-assets: src/save_ui_data.cpp src/save_ui_bytes.s assets/save-ui.bin
src/save_ui_data.cpp src/save_ui_bytes.s assets/save-ui.bin &: tools/save_load_assets.py tools/save_converter_assets.py tools/save_load_mockups.py tools/witch_book_icons.py src/story_data.cpp src/graphics_data.cpp src/trial_data.cpp assets/graphics-cache/linked/graphics.json assets/graphics-cache/linked/graphics.bin $(wildcard ../manosaba-web/public/UI_SaveLoad/*.png) assets/unifont-17.0.03.hex.gz
	../python_workspace/.venv/bin/python tools/save_load_assets.py

choice-assets: src/choice_ui_data.cpp src/choice_ui_data.h
story-ui: src/story_ui_data.cpp
book-assets: src/witch_book_art.cpp src/witch_book_art.h src/witch_book_bytes.s assets/trial/book.bin
book-map-assets: src/book_map_data.cpp src/book_map_data.h src/book_map_bytes.s assets/trial/book-maps.bin
assets/source-metadata/book-maps.json: tools/witch_book_map_sources.py ../original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-ui_assets_all.bundle $(shell find ../story/zh-Hans -name '*.json')
	../python_workspace/.venv/bin/python tools/witch_book_map_sources.py
src/book_map_data.cpp src/book_map_data.h src/book_map_bytes.s assets/trial/book-maps.bin &: tools/witch_book_maps.py tools/witch_book_map_art.py assets/source-metadata/book-maps.json assets/source-metadata/trial.json $(wildcard ../manosaba-web/public/UI_WitchBook_2/Layout*.png)
	../python_workspace/.venv/bin/python tools/witch_book_maps.py
src/witch_book_art.cpp src/witch_book_art.h src/witch_book_bytes.s assets/trial/book.bin &: tools/witch_book_assets.py tools/witch_book_icons.py tools/witch_book_catalog.py tools/nameplates.py assets/unifont-17.0.03.hex.gz assets/source-metadata/trial.json assets/trial-ui-palette.json ../original_sprites/manifest.csv $(wildcard ../manosaba-web/public/UI_WitchBook_1/*.png) $(wildcard ../manosaba-web/public/UI_WitchBook_2/Layout*.png)
	../python_workspace/.venv/bin/python tools/witch_book_assets.py
src/story_ui_data.cpp: tools/story_ui.py tools/image_encoding.py $(addprefix ../manosaba-web/public/UI_Adv/,NormalPrinter_Frame_Bottom.png NormalPrinter_Frame_Top.png NormalPrinter_Grass_1.png NormalPrinter_Grass_2.png BookButton.png MenuButton.png)
	../python_workspace/.venv/bin/python tools/story_ui.py

nameplates: src/nameplate_data.cpp
src/nameplate_data.cpp: tools/nameplates.py tools/font.py assets/nameplates.json assets/source-metadata/trial.json assets/trial-ui-palette.json assets/unifont-17.0.03.hex.gz assets/fusion-pixel-12px-monospaced-zh_hans.bdf ../manosaba-web/public/UI_Adv/NamePlateBase.png
	../python_workspace/.venv/bin/python tools/nameplates.py

src/choice_ui_data.cpp src/choice_ui_data.h &: tools/choice_ui.py tools/story_layout.py assets/graphics/catalog.json $(wildcard ../manosaba-web/public/UI_Adv/Choice/*.png) ../manosaba-web/public/UI_Adv/BookButton.png ../manosaba-web/public/UI_Adv/MenuButton.png $(shell find ../story/zh-Hans -name '*.json')
	../python_workspace/.venv/bin/python tools/choice_ui.py

trial-resources: src/trial_data.cpp src/trial_ui_data.cpp src/trial_cutin_data.cpp src/trial_title_data.cpp src/trial_title_bytes.s src/trial_bytes.s src/trial_ui_bytes.s src/trial_cutin_bytes.s assets/trial/court.bin assets/trial/poses.json assets/trial/ui.bin assets/trial/cutins.bin assets/trial/titles.bin assets/trial/camera/Court/expected.bin assets/trial/camera/Court_Final/expected.bin assets/source-metadata/trial.json

.PHONY: trial-title-test
trial-title-test: hiro-test-rom
	../python_workspace/.venv/bin/python tools/run_trial_title_test.py manosaba-hiro.gba build-butano/trial-title-test

src/trial_title_data.cpp src/trial_title_bytes.s assets/trial/titles.bin &: tools/trial_titles.py tools/trial_cutins.py src/trial_title_data.h ../original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle $(wildcard ../manosaba-web/public/UI_Trial/Debate/*.png)
	PYTHONPATH=tools:../python_workspace ../python_workspace/.venv/bin/python tools/trial_titles.py

assets/source-metadata/trial.json: tools/trial_sources.py
	../python_workspace/.venv/bin/python tools/trial_sources.py

src/trial_data.cpp src/trial_bytes.s assets/trial/court.bin assets/trial/poses.json assets/trial/camera/Court/expected.bin assets/trial/camera/Court_Final/expected.bin &: tools/trial_stage_assets.py tools/court_render.py tools/trial_story.py tools/trial_camera.py tools/trial_views.py tools/trial_pose_assets.py assets/graphics-cache/encoded.json $(shell find ../story/zh-Hans -name '*Trial*.json')
	../python_workspace/.venv/bin/python tools/trial_stage_assets.py

src/trial_ui_data.cpp src/trial_ui_bytes.s assets/trial/ui.bin &: tools/trial_ui.py tools/nameplates.py src/choice_ui_data.cpp assets/trial-ui-palette.json assets/source-metadata/trial.json ../manosaba-web/public/UI_Adv/NamePlateBase.png ../manosaba-web/public/UI_Trial/ClockBase.png $(wildcard ../manosaba-web/public/UI_Trial/Objection/*.png)
	../python_workspace/.venv/bin/python tools/trial_ui.py

src/trial_cutin_data.cpp src/trial_cutin_bytes.s assets/trial/cutins.bin &: tools/trial_cutins.py tools/court_render.py
	../python_workspace/.venv/bin/python tools/trial_cutins.py

sfx-assets: assets/sfx/manifest.json src/sfx_data.cpp src/sfx_samples.inc
assets/sfx/manifest.json src/sfx_data.cpp src/sfx_samples.inc &: tools/sfx_assets.py tools/trial_cutins.py ../sfx/recipes/clips.json ../sfx/tools/trim_sfx_silence.py ../python_workspace/audio_manifest.json ../sfx/recipes/footsteps.json $(wildcard ../sfx/samples/*.wav ../sfx/samples/footsteps/*.wav) ../original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle
	../sfx/.venv/bin/python tools/sfx_assets.py

story: trial-map-assets src/story_data.cpp src/story_glyphs.txt
src/story_data.cpp src/story_glyphs.txt src/story_layout.h &: tools/story.py tools/story_text_color.py tools/story_music.py tools/story_sfx.py assets/sfx/manifest.json tools/trial_story.py tools/trial_effects.py tools/witch_book_catalog.py tools/trial_camera.py tools/trial_views.py tools/trial_demo_story.py tools/trial_contexts.py tools/story_layout.py tools/graphical_story.py assets/nameplates.json assets/trial/poses.json assets/source-metadata/trial.json assets/text-layout.json assets/graphical-story.json assets/reenact/catalog.json assets/trial-maps/catalog.json assets/graphics/catalog.json src/story_format.h $(shell find ../story/zh-Hans -name '*.json')
	$(PYTHON) tools/story.py

reflow:
	$(PYTHON) tools/story_layout.py --output build-butano/story-reflow.json

reflow-test:
	$(PYTHON) tools/story_layout_test.py

stage-test: prepared-assets
	mkdir -p build-butano
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/stage_test.cpp src/stage_player.cpp src/stage_raster.cpp src/stage_data.cpp -o build-butano/stage_test
	build-butano/stage_test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/stage_timing_test.cpp src/stage_player.cpp -o build-butano/stage_timing_test
	build-butano/stage_timing_test
	$(MAKE) actor-scale-test

.PHONY: actor-scale-test
actor-scale-test:
	mkdir -p build-butano
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -DMANOSABA_ACTOR_SCALE_SNAP -DMANOSABA_ACTOR_SCALE_SNAP_115 -Isrc tools/actor_scale_test.cpp src/stage_player.cpp -o build-butano/actor_scale_test
	build-butano/actor_scale_test

.PHONY: silhouette-test
silhouette-test: all
	../python_workspace/.venv/bin/python tools/run_silhouette_test.py manosaba.gba build-butano/silhouette-test

.PHONY: actor-swap-test
actor-swap-test: all
	../python_workspace/.venv/bin/python tools/run_silhouette_test.py manosaba.gba build-butano/actor-swap swap

dialogue-test: prepared-assets
	mkdir -p build-butano
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/dialogue_page_test.cpp src/dialogue_page.cpp -o build-butano/dialogue_page_test
	build-butano/dialogue_page_test

graphics-plan:
	../python_workspace/.venv/bin/python ../python_workspace/import_gba_graphics.py --plan-only

graphics-import:
	../python_workspace/.venv/bin/python ../python_workspace/import_gba_graphics.py

graphics-import-test:
	../python_workspace/.venv/bin/python tools/graphics_import_test.py
	../python_workspace/.venv/bin/python tools/graphics_encoding_test.py

.PHONY: camera-test
camera-test: all
	../python_workspace/.venv/bin/python tools/run_camera_test.py manosaba.gba build-butano/camera-test

graphics-link: graphics-import
	../python_workspace/.venv/bin/python ../python_workspace/encode_gba_graphics.py
	../python_workspace/.venv/bin/python tools/graphics_cpp.py

images: graphics-link story-ui nameplates

font: warning-assets execution-assets ending-assets src/book_map_data.cpp src/font_data.c
src/font_data.c &: src/ending_glyphs.txt src/book_map_data.cpp tools/font.py src/story_glyphs.txt assets/fusion-pixel-12px-monospaced-zh_hans.bdf assets/unifont-17.0.03.hex.gz $(filter-out src/story_data.cpp,$(wildcard src/*.cpp src/*.h)) $(shell find ../original_scripts/zh-Hans -name '*.nani')
	$(PYTHON) tools/font.py

setup:
	$(PYTHON) tools/setup_butano.py

story-test: prepared-assets
	mkdir -p build-butano
	$(PYTHON) tools/story_reference.py build-butano/story_traces.txt
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/story_test.cpp src/story_vm.cpp src/lzss.cpp src/story_data.cpp -o build-butano/story_test
	build-butano/story_test build-butano/story_traces.txt

stage-smoke: all
	mkdir -p build-butano/smoke
	cc -O2 -Wall -Wextra tools/stage_smoke_test.c -lmgba -o build-butano/stage_smoke_test
	$(PYTHON) tools/run_graphics_test.py build-butano/stage_smoke_test manosaba.gba build-butano/smoke

.PHONY: adv-background-test
adv-background-test: all
	$(CC) -O2 -Wall -Wextra tools/adv_background_test.c -lmgba -o build-butano/adv_background_test
	$(PYTHON) tools/run_graphics_test.py build-butano/adv_background_test manosaba.gba build-butano/adv02-background

.PHONY: live-test
live-test: all
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/live_vm_test.cpp src/story_vm.cpp src/lzss.cpp src/story_data.cpp -o build-butano/live_vm_test
	build-butano/live_vm_test
	$(CC) -O2 -Wall -Wextra tools/live_game_test.c -lmgba -o build-butano/live_game_test
	mkdir -p build-butano/live
	build-butano/live_game_test manosaba.gba build-butano/live

.PHONY: story-retry-test
story-retry-test: all
	../python_workspace/.venv/bin/python tools/run_story_retry_test.py manosaba.gba build-butano/story-choice-retry

choice-smoke: all
	mkdir -p build-butano/smoke
	cc -O2 -Wall -Wextra tools/choice_smoke_test.c -lmgba -o build-butano/choice_smoke_test
	$(PYTHON) tools/run_graphics_test.py build-butano/choice_smoke_test manosaba.gba build-butano/smoke

actor-panel-test: all
	mkdir -p build-butano/body-fix
	cc -O2 -Wall -Wextra tools/actor_panel_test.c -lmgba -o build-butano/actor_panel_test
	build-butano/actor_panel_test manosaba.gba build-butano/body-fix

overlay-panel-test: all
	mkdir -p build-butano/overlay-panel
	cc -O2 -Wall -Wextra tools/overlay_panel_test.c -lmgba -o build-butano/overlay_panel_test
	build-butano/overlay_panel_test manosaba.gba build-butano/overlay-panel

graphics-perf: all
	mkdir -p build-butano/smoke
	cc -O2 -Wall -Wextra tools/graphics_perf_test.c -lmgba -o build-butano/graphics_perf_test
	$(PYTHON) tools/run_graphics_test.py build-butano/graphics_perf_test manosaba.gba build-butano/smoke

graphics-smoke: all
	mkdir -p build-butano/smoke
	cc -O2 -Wall -Wextra tools/graphics_banks_test.c -lmgba -o build-butano/graphics_banks_test
	$(PYTHON) tools/run_graphics_test.py build-butano/graphics_banks_test manosaba.gba build-butano/smoke

audio-budget: nameplates story-ui trial-resources choice-assets book-assets book-map-assets save-assets font story
	$(MAKE) BUTANO_BUILD=1 BUTANO=$(abspath $(BUTANO)) BUILD=build-audio-budget TARGET=manosaba-audio-budget USERCXXFLAGS=-DMANOSABA_AUDIO_RESERVE_PERCENT=20
	$(PYTHON) tools/rom_budget.py manosaba-audio-budget.gba

# Reproducible real-music integration build; the override is visible in the
# existing music page and leaves native cue mapping available when disabled.
music-tools:
	$(PYTHON) tools/setup_mmutil.py

.PHONY: music-tools music-cues hiro-test-rom hiro-game-test music-event-test

music-cues: src/music_cues.cpp src/music_modules.inc src/music_catalog.cpp
src/music_cues.cpp src/music_modules.inc src/music_catalog.cpp &: tools/music_cues.py assets/music/runtime.json assets/music/catalog.json $(wildcard audio/*.xm)
	$(PYTHON) tools/music_cues.py

hiro-test-rom: nameplates story-ui trial-resources choice-assets book-assets book-map-assets save-assets font story music-cues
	$(MAKE) BUTANO_BUILD=1 BUTANO=$(abspath $(BUTANO)) BUILD=build-hiro TARGET=manosaba-hiro USERCXXFLAGS=-DMANOSABA_MUSIC_STRESS
	$(PYTHON) tools/rom_budget.py manosaba-hiro.gba

hiro-game-test: all
	../python_workspace/.venv/bin/python tools/run_hiro_game_test.py manosaba.gba build-butano/hiro-live

.PHONY: music-game-test
music-game-test: all
	../python_workspace/.venv/bin/python tools/run_music_game_test.py manosaba.gba build-butano/music-live

music-event-test: prepared-assets music-cues
	$(PYTHON) tools/story_reference.py build-butano/story_music_traces.txt --music
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/story_test.cpp src/story_vm.cpp src/lzss.cpp src/story_data.cpp -o build-butano/story_music_test
	build-butano/story_music_test build-butano/story_music_traces.txt --music
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/music_player_test.cpp src/music_player.cpp src/music_cues.cpp -o build-butano/music_player_test
	build-butano/music_player_test

audio-perf: audio-budget
	mkdir -p build-butano/audio-smoke
	cc -O2 -Wall -Wextra tools/graphics_perf_test.c -lmgba -o build-butano/graphics_perf_test
	$(PYTHON) tools/run_graphics_test.py build-butano/graphics_perf_test manosaba-audio-budget.gba build-butano/audio-smoke

trial-test: prepared-assets
	mkdir -p build-butano
	$(PYTHON) tools/trial_views_test.py
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/choice_window_test.cpp -o build-butano/choice_window_test
	build-butano/choice_window_test
	$(PYTHON) tools/trial_layout_test.py
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/trial_vm_test.cpp src/story_vm.cpp src/lzss.cpp src/story_data.cpp -o build-butano/trial_vm_test
	build-butano/trial_vm_test

witch-book-vm-test: prepared-assets
	mkdir -p build-butano
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc tools/witch_book_vm_test.cpp src/story_vm.cpp src/lzss.cpp src/story_data.cpp -o build-butano/witch_book_vm_test
	build-butano/witch_book_vm_test

witch-book-latency-test: all
	../python_workspace/.venv/bin/python tools/run_witch_book_latency_test.py manosaba.gba build-butano/witch-book-latency

witch-book-game-test: all
	../python_workspace/.venv/bin/python tools/run_witch_book_test.py manosaba.gba build-butano/witch-book

dialogue-history-test: all
	../python_workspace/.venv/bin/python tools/run_dialogue_history_test.py manosaba.gba build-butano/dialogue-history

dialogue-color-test: all
	../python_workspace/.venv/bin/python tools/run_dialogue_color_test.py manosaba.gba build-butano/dialogue-color

bad-end-test: all
	../python_workspace/.venv/bin/python tools/run_bad_end_test.py manosaba.gba build-butano/bad-end

trial-game-test: all
	$(PYTHON) tools/run_trial_game_test.py manosaba.gba build-butano/trial-game

# Independent native processes share only the frozen ROM; asset tools stay serial.
TRIAL_WALK_JOBS ?= 12
.PHONY: check check-full check-list trial-walk-full
TEST_JOBS ?= 12
CHECK_ARGS ?=
check: all
	$(PYTHON) tools/run_regression.py manosaba.gba build-butano/check --jobs $(TEST_JOBS) $(CHECK_ARGS)

check-full: all
	$(PYTHON) tools/run_regression.py manosaba.gba build-butano/check-full --full --jobs $(TEST_JOBS) $(CHECK_ARGS)

check-list:
	$(PYTHON) tools/run_regression.py manosaba.gba build-butano/check --list $(CHECK_ARGS)

trial-walk: all
	$(PYTHON) tools/run_trial_walk.py manosaba.gba build-butano/trial-walk --jobs $(TRIAL_WALK_JOBS)

trial-walk-full: all
	$(PYTHON) tools/run_trial_walk.py manosaba.gba build-butano/trial-walk-full --scope both --jobs $(TRIAL_WALK_JOBS)

trial-walk-chapters: all
	$(PYTHON) tools/run_trial_walk.py manosaba.gba build-butano/trial-walk-chapters --scope chapter --jobs $(TRIAL_WALK_JOBS)

trial-presentation-test: all
	$(PYTHON) tools/run_trial_presentation_test.py manosaba.gba build-butano/trial-presentation

trial-camera-test: all
	$(PYTHON) tools/run_trial_camera_test.py manosaba.gba build-butano/trial-cameras-chapters
	$(PYTHON) tools/run_trial_camera_test.py manosaba.gba build-butano/trial-cameras-short --short

smoke: all
	mkdir -p build-butano/smoke
	cc -O2 -Wall -Wextra tools/smoke_test.c -lmgba -o build-butano/smoke_test
	build-butano/smoke_test manosaba.gba build-butano/smoke

clean:
	rm -rf build-butano build-release build-generated manosaba.gba manosaba.elf manosaba.elf.* manosaba-release.gba manosaba-release.elf manosaba-release.elf.*
endif

.PHONY: sfx-assets sfx-test
sfx-test: all
	../python_workspace/.venv/bin/python tools/run_sfx_test.py manosaba.gba build-butano/sfx-test
