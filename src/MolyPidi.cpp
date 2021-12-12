#include "Computerscare.hpp"

// namespace rack {
// namespace core {


struct MIDICC_CV : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(CC_OUTPUT, 16),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	midi::InputQueue midiInput;

	/** [cc][channel] */
	int8_t ccValues[128][16];
	/** When LSB is enabled for CC 0-31, the MSB is stored here until the LSB is received.
	[cc][channel]
	*/
	int8_t msbValues[32][16];
	int learningId;
	int learnedCcs[16];
	/** [cell][channel] */
	dsp::ExponentialFilter valueFilters[16][16];
	bool smooth;
	bool mpeMode;
	bool lsbMode;

	MIDICC_CV() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 16; i++)
			configOutput(CC_OUTPUT + i, string::f("Cell %d", i + 1));

		for (int i = 0; i < 16; i++) {
			for (int c = 0; c < 16; c++) {
				valueFilters[i][c].setTau(1 / 30.f);
			}
		}
		onReset();
	}

	void onReset() override {
		for (int cc = 0; cc < 128; cc++) {
			for (int c = 0; c < 16; c++) {
				ccValues[cc][c] = 0;
			}
		}
		for (int cc = 0; cc < 32; cc++) {
			for (int c = 0; c < 16; c++) {
				msbValues[cc][c] = 0;
			}
		}
		learningId = -1;
		for (int i = 0; i < 16; i++) {
			learnedCcs[i] = i;
		}
		midiInput.reset();
		smooth = true;
		mpeMode = false;
		lsbMode = false;
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			processMessage(msg);
		}

		int channels = 16;

		for (int i = 0; i < 16; i++) {
			if (!outputs[CC_OUTPUT + i].isConnected())
				continue;
			outputs[CC_OUTPUT + i].setChannels(channels);

			//int cc = learnedCcs[i];

			for (int c = 0; c < channels; c++) {
				int cc = c;
				int16_t cellValue = int16_t(ccValues[cc][i]) * 128;
				if (lsbMode && cc < 32)
					cellValue += ccValues[cc + 32][c];
				// Maximum value for 14-bit CC should be MSB=127 LSB=0, not MSB=127 LSB=127, because this is the maximum value that 7-bit controllers can send.
				float value = float(cellValue) / (128 * 127);
				// Support negative values because the gamepad MIDI driver generates nonstandard 8-bit CC values.
				value = clamp(value, -1.f, 1.f);

				// Detect behavior from MIDI buttons.
				if (smooth && std::fabs(valueFilters[i][c].out - value) < 1.f) {
					// Smooth value with filter
					valueFilters[i][c].process(args.sampleTime, value);
				}
				else {
					// Jump value
					valueFilters[i][c].out = value;
				}
				outputs[CC_OUTPUT + i].setVoltage(valueFilters[i][c].out * 10.f, c);
			}
		}
	}

	void processMessage(const midi::Message& msg) {
		switch (msg.getStatus()) {
		// cc
		case 0xb: {
			processCC(msg);
		} break;
		default: break;
		}
	}

	void processCC(const midi::Message& msg) {
		uint8_t c = msg.getChannel();
		uint8_t cc = msg.getNote();
		std::string msgString = msg.toString();
		DEBUG("message:%s", msgString.c_str());
		if (msg.bytes.size() < 2)
			return;
		// Allow CC to be negative if the 8th bit is set.
		// The gamepad driver abuses this, for example.
		// Cast uint8_t to int8_t
		int8_t value = msg.bytes[2];
		// Learn
		if (learningId >= 0 && ccValues[cc][c] != value) {
			learnedCcs[learningId] = cc;
			learningId = -1;
		}

		if (lsbMode && cc < 32) {
			// Don't set MSB yet. Wait for LSB to be received.
			msbValues[cc][c] = value;
		}
		else if (lsbMode && 32 <= cc && cc < 64) {
			// Apply MSB when LSB is received
			ccValues[cc - 32][c] = msbValues[cc - 32][c];
			ccValues[cc][c] = value;
		}
		else {
			ccValues[cc][c] = value;
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_t* ccsJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_array_append_new(ccsJ, json_integer(learnedCcs[i]));
		}
		json_object_set_new(rootJ, "ccs", ccsJ);

		// Remember values so users don't have to touch MIDI controller knobs when restarting Rack
		json_t* valuesJ = json_array();

		for (int midiCh = 0; midiCh < 16; midiCh++) {
			for (int cc = 0; cc < 16; cc++) {
				json_array_append_new(valuesJ, json_integer(ccValues[cc][midiCh]));
			}
		}
		json_object_set_new(rootJ, "values", valuesJ);

		json_object_set_new(rootJ, "midi", midiInput.toJson());

		json_object_set_new(rootJ, "smooth", json_boolean(smooth));
		json_object_set_new(rootJ, "mpeMode", json_boolean(mpeMode));
		json_object_set_new(rootJ, "lsbMode", json_boolean(lsbMode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* ccsJ = json_object_get(rootJ, "ccs");
		if (ccsJ) {
			for (int i = 0; i < 16; i++) {
				json_t* ccJ = json_array_get(ccsJ, i);
				if (ccJ)
					learnedCcs[i] = json_integer_value(ccJ);
			}
		}

		json_t* valuesJ = json_object_get(rootJ, "values");
		if (valuesJ) {
			for (int i = 0; i < 256; i++) {
				json_t* valueJ = json_array_get(valuesJ, i);
				if (valueJ) {
					int cc = i % 16;
					int midiCh = (i - cc) / 16;

					ccValues[cc][midiCh] = json_integer_value(valueJ);
				}
			}
		}

		json_t* midiJ = json_object_get(rootJ, "midi");
		if (midiJ)
			midiInput.fromJson(midiJ);

		json_t* smoothJ = json_object_get(rootJ, "smooth");
		if (smoothJ)
			smooth = json_boolean_value(smoothJ);

		json_t* mpeModeJ = json_object_get(rootJ, "mpeMode");
		if (mpeModeJ)
			mpeMode = json_boolean_value(mpeModeJ);

		json_t* lsbEnabledJ = json_object_get(rootJ, "lsbMode");
		if (lsbEnabledJ)
			lsbMode = json_boolean_value(lsbEnabledJ);
	}
};

template <class TChoice>
struct ComputerscareGrid16MidiDisplay : MidiDisplay {
	LedDisplaySeparator* hSeparators[4];
	LedDisplaySeparator* vSeparators[4];
	TChoice* choices[4][4];

	template <class TModule>
	void setModule(TModule* module) {
		Vec pos = channelChoice->box.getBottomLeft();
		// Add vSeparators
		for (int x = 1; x < 4; x++) {
			vSeparators[x] = createWidget<LedDisplaySeparator>(pos);
			vSeparators[x]->box.pos.x = box.size.x / 4 * x;
			addChild(vSeparators[x]);
		}
		// Add hSeparators and choice widgets
		for (int y = 0; y < 4; y++) {
			hSeparators[y] = createWidget<LedDisplaySeparator>(pos);
			hSeparators[y]->box.size.x = box.size.x;
			addChild(hSeparators[y]);
			for (int x = 0; x < 4; x++) {
				choices[x][y] = new TChoice;
				choices[x][y]->box.pos = pos;
				choices[x][y]->setId(4 * y + x);
				choices[x][y]->box.size.x = box.size.x / 4;
				choices[x][y]->box.pos.x = box.size.x / 4 * x;
				choices[x][y]->setModule(module);
				addChild(choices[x][y]);
			}
			pos = choices[0][y]->box.getBottomLeft();
		}
		for (int x = 1; x < 4; x++) {
			vSeparators[x]->box.size.y = pos.y - vSeparators[x]->box.pos.y;
		}
	}
};


template <class TModule>
struct ComputerscareCcChoice : LedDisplayChoice {
	TModule* module;
	int id;
	int focusCc;

	ComputerscareCcChoice() {
		box.size.y = mm2px(6.666);
		textOffset.y -= 4;
	}

	void setModule(TModule* module) {
		this->module = module;
	}

	void setId(int id) {
		this->id = id;
	}

	void step() override {
		int cc;
		if (!module) {
			cc = id;
		}
		else if (module->learningId == id) {
			cc = focusCc;
			color.a = 0.5;
		}
		else {
			cc = module->learnedCcs[id];
			color.a = 1.0;

			// Cancel focus if no longer learning
			if (APP->event->getSelectedWidget() == this)
				APP->event->setSelectedWidget(NULL);
		}

		// Set text
		if (cc < 0)
			text = "--";
		else
			text = string::f("%d", cc);
	}

	void onSelect(const SelectEvent& e) override {
		if (!module)
			return;
		module->learningId = id;
		focusCc = -1;
		e.consume(this);
	}

	void onDeselect(const DeselectEvent& e) override {
		if (!module)
			return;
		if (module->learningId == id) {
			if (0 <= focusCc && focusCc < 128) {
				module->learnedCcs[id] = focusCc;
			}
			module->learningId = -1;
		}
	}

	void onSelectText(const SelectTextEvent& e) override {
		int c = e.codepoint;
		if ('0' <= c && c <= '9') {
			if (focusCc < 0)
				focusCc = 0;
			focusCc = focusCc * 10 + (c - '0');
		}
		if (focusCc >= 128)
			focusCc = -1;
		e.consume(this);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if ((e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER) && e.action == GLFW_PRESS && (e.mods & RACK_MOD_MASK) == 0) {
			DeselectEvent eDeselect;
			onDeselect(eDeselect);
			APP->event->selectedWidget = NULL;
			e.consume(this);
		}
	}
};


struct MIDICC_CVWidget : ModuleWidget {
	MIDICC_CVWidget(MIDICC_CV* module) {
		setModule(module);
		setPanel(Svg::load(asset::system("res/Core/MIDICC_CV.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.189, 78.431)), module, MIDICC_CV::CC_OUTPUT + 0));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(19.739, 78.431)), module, MIDICC_CV::CC_OUTPUT + 1));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.289, 78.431)), module, MIDICC_CV::CC_OUTPUT + 2));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.838, 78.431)), module, MIDICC_CV::CC_OUTPUT + 3));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.189, 89.946)), module, MIDICC_CV::CC_OUTPUT + 4));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(19.739, 89.946)), module, MIDICC_CV::CC_OUTPUT + 5));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.289, 89.946)), module, MIDICC_CV::CC_OUTPUT + 6));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.838, 89.946)), module, MIDICC_CV::CC_OUTPUT + 7));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.389, 101.466)), module, MIDICC_CV::CC_OUTPUT + 8));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(19.739, 101.466)), module, MIDICC_CV::CC_OUTPUT + 9));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.289, 102.466)), module, MIDICC_CV::CC_OUTPUT + 10));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.838, 101.466)), module, MIDICC_CV::CC_OUTPUT + 11));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.19, 112.998)), module, MIDICC_CV::CC_OUTPUT + 12));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(19.739, 112.984)), module, MIDICC_CV::CC_OUTPUT + 13));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.289, 112.984)), module, MIDICC_CV::CC_OUTPUT + 14));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.839, 112.984)), module, MIDICC_CV::CC_OUTPUT + 15));

		typedef ComputerscareGrid16MidiDisplay<ComputerscareCcChoice<MIDICC_CV>> TMidiDisplay;
		TMidiDisplay* display = createWidget<TMidiDisplay>(mm2px(Vec(0.0, 13.039)));
		display->box.size = mm2px(Vec(50.8, 55.88));
		display->setMidiPort(module ? &module->midiInput : NULL);
		display->setModule(module);
		addChild(display);
	}

	void appendContextMenu(Menu* menu) override {
		MIDICC_CV* module = dynamic_cast<MIDICC_CV*>(this->module);

		menu->addChild(new MenuSeparator);

		menu->addChild(createBoolPtrMenuItem("Smooth CC", "", &module->smooth));

		menu->addChild(createBoolPtrMenuItem("MPE mode", "", &module->mpeMode));

		menu->addChild(createBoolPtrMenuItem("CC 0-31 controls are 14-bit", "", &module->lsbMode));
	}
};


// } // namespace core
// } // namespace rack

Model* modelComputerscareMolyPidi = createModel<MIDICC_CV, MIDICC_CVWidget>("computerscare-moly-pidi");

