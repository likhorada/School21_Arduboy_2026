ARDUINO_CLI ?= arduino-cli
CXX ?= g++
FQBN := arduino:avr:leonardo
SKETCH := src
BUILD := $(CURDIR)/build

.PHONY: setup build test upload

# Устанавливаем версии ядра и библиотеки, на которых проверен прототип.
setup:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install arduino:avr@1.8.8
	$(ARDUINO_CLI) lib install Arduboy2@6.0.0
	$(ARDUINO_CLI) lib install ArduboyTones@1.0.3

build:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --warnings all --build-path "$(BUILD)/avr" $(SKETCH)
	python3 tools/check_size.py --cli "$(ARDUINO_CLI)" "$(BUILD)/avr/$(SKETCH).ino.elf"

test:
	mkdir -p "$(BUILD)/tests"
	$(CXX) -std=c++11 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -I$(SKETCH) $(SKETCH)/game.cpp $(SKETCH)/arena.cpp $(SKETCH)/combat.cpp $(SKETCH)/enemies.cpp $(SKETCH)/stages.cpp tests/gameplay_tests.cpp -o "$(BUILD)/tests/gameplay_tests"
	"$(BUILD)/tests/gameplay_tests"
	$(CXX) -std=c++11 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -I$(SKETCH) $(SKETCH)/game.cpp $(SKETCH)/arena.cpp $(SKETCH)/combat.cpp $(SKETCH)/enemies.cpp $(SKETCH)/stages.cpp tests/combat_tests.cpp -o "$(BUILD)/tests/combat_tests"
	"$(BUILD)/tests/combat_tests"
	$(CXX) -std=c++14 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/stubs -I$(SKETCH) $(SKETCH)/game.cpp $(SKETCH)/arena.cpp $(SKETCH)/combat.cpp $(SKETCH)/enemies.cpp $(SKETCH)/stages.cpp $(SKETCH)/render.cpp tests/render_tests.cpp -o "$(BUILD)/tests/render_tests"
	"$(BUILD)/tests/render_tests"

# Пример загрузки: make upload PORT=/dev/ttyACM0
upload: build
	@test -n "$(PORT)" || { printf 'Set PORT, e.g. make upload PORT=/dev/ttyACM0\n'; exit 1; }
	$(ARDUINO_CLI) upload --fqbn $(FQBN) --port "$(PORT)" --input-dir "$(BUILD)/avr" $(SKETCH)
