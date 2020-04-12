#include "Computerscare.hpp"
#include "dtpulse.hpp"

struct ComputerscareRolyPouter;

const int numKnobs = 16;

struct ComputerscareRolyPouter : ComputerscarePolyModule {
	int counter = 0;
	int routing[numKnobs];
	int numOutputChannels = 16;
	ComputerscareSVGPanel* panelRef;
	enum ParamIds {
		KNOB,
		POLY_CHANNELS = KNOB + numKnobs,
		NUM_PARAMS
	};
	enum InputIds {
		POLY_INPUT,
		ROUTING_CV,
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};


	ComputerscareRolyPouter()  {

		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int i = 0; i < numKnobs; i++) {
			configParam(KNOB + i, 1.f, 16.f, (i + 1), "output ch" + std::to_string(i + 1) + " = input ch");
			routing[i] = i;
		}
		configParam(POLY_CHANNELS, 0.f, 16.f, 0.f, "Poly Channels");

	}
	void setAll(int setVal) {
		for (int i = 0; i < 16; i++) {
			params[KNOB + i].setValue(setVal);
		}
	}
	void onRandomize() override {
		int numInputChannels = inputs[POLY_INPUT].getChannels();
		for(int i = 0; i < polyChannels; i++) {
			params[KNOB+i].setValue(1+std::floor(random::uniform()*numInputChannels));
		}
	}
	void checkPoly() override {
		int inputChannels = inputs[POLY_INPUT].getChannels();
		int cvChannels = inputs[ROUTING_CV].getChannels();
		int knobSetting = params[POLY_CHANNELS].getValue();
		if (knobSetting == 0) {
			polyChannels = inputChannels;
		}
		else {
			polyChannels = knobSetting;
		}
		outputs[POLY_OUTPUT].setChannels(polyChannels);
	}
	void process(const ProcessArgs &args) override {
		ComputerscarePolyModule::checkCounter();
		counter++;
		int inputChannels = inputs[POLY_INPUT].getChannels();
		int cvChannels = inputs[ROUTING_CV].getChannels();
		int knobSetting;

		//outputs[POLY_OUTPUT].setChannels(numOutputChannels);

		//if()
		if (cvChannels > 0)  {
			for (int i = 0; i < numOutputChannels; i++) {

				knobSetting = std::round(inputs[ROUTING_CV].getVoltage(cvChannels == 1 ? 0 : i) * 1.5) + 1;

				routing[i] = knobSetting;
				if (knobSetting > inputChannels) {
					outputs[POLY_OUTPUT].setVoltage(0, i);
				}
				else {
					outputs[POLY_OUTPUT].setVoltage(inputs[POLY_INPUT].getVoltage(knobSetting), i);
				}
			}
		} else {
			if (counter > 1000) {
				//printf("%f \n",random::uniform());
				counter = 0;
				for (int i = 0; i < numKnobs; i++) {
					routing[i] = (int)params[KNOB + i].getValue();
				}

			}
			for (int i = 0; i < numOutputChannels; i++) {
				knobSetting = params[KNOB + i].getValue();
				if (knobSetting > inputChannels) {
					outputs[POLY_OUTPUT].setVoltage(0, i);
				}
				else {
					outputs[POLY_OUTPUT].setVoltage(inputs[POLY_INPUT].getVoltage(knobSetting - 1), i);
				}
			}
		}
	}

};
struct PouterSmallDisplay : SmallLetterDisplay
{
	ComputerscareRolyPouter *module;
	int ch;
	PouterSmallDisplay(int outputChannelNumber)
	{
		ch = outputChannelNumber;
		SmallLetterDisplay();
	};
	void draw(const DrawArgs &args)
	{
		//this->setNumDivisionsString();
		if (module)
		{
			std::string str = std::to_string(module->routing[ch]);
			value = str;
		}
		SmallLetterDisplay::draw(args);
	}
};
struct DisableableSnapKnob : RoundBlackSnapKnob {
	ComputerscarePolyModule *module;
	int channel;
	bool disabled = false;
	int lastDisabled = -1;
	std::shared_ptr<Svg> enabledSvg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/computerscare-medium-knob-dot-indicator.svg"));
	std::shared_ptr<Svg> disabledSvg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/computerscare-medium-knob-dot-indicator-disabled.svg"));

	DisableableSnapKnob() {
		RoundBlackSnapKnob();
	}
	void step() override {
		if (module) {
			disabled = channel > module->polyChannels - 1;
		}
		if (disabled != lastDisabled) {
			setSvg(disabled ? disabledSvg : enabledSvg);
			dirtyValue = -20.f;
			lastDisabled = disabled;
		}
		RoundBlackSnapKnob::step();
	}
	void randomize() override {return;}
};
struct ComputerscareRolyPouterWidget : ModuleWidget {
	ComputerscareRolyPouterWidget(ComputerscareRolyPouter *module) {

		setModule(module);
		box.size = Vec(4 * 15, 380);
		{
			ComputerscareSVGPanel *panel = new ComputerscareSVGPanel();
			panel->box.size = box.size;
			panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComputerscareRolyPouterPanel.svg")));
			addChild(panel);

		}
		channelWidget = new PolyOutputChannelsWidget(Vec(2, 13), module, ComputerscareRolyPouter::POLY_CHANNELS);
		addChild(channelWidget);

		addInput(createInput<PointingUpPentagonPort>(Vec(22, 52), module, ComputerscareRolyPouter::ROUTING_CV));

		float xx;
		float yy;
		for (int i = 0; i < numKnobs; i++) {
			xx = 1.4f + 24.3 * (i - i % 8) / 8;
			yy = 66 + 36.5 * (i % 8) + 14.3 * (i - i % 8) / 8;
			addLabeledKnob(std::to_string(i + 1), xx, yy, module, i, (i - i % 8) * 1.3 - 5, i < 8 ? 4 : 0);
		}
		addInput(createInput<InPort>(Vec(1, 36), module, ComputerscareRolyPouter::POLY_INPUT));
		addOutput(createOutput<PointingUpPentagonPort>(Vec(32, 18), module, ComputerscareRolyPouter::POLY_OUTPUT));

	}
	void addLabeledKnob(std::string label, int x, int y, ComputerscareRolyPouter *module, int index, float labelDx, float labelDy) {

		pouterSmallDisplay = new PouterSmallDisplay(index);
		pouterSmallDisplay->box.size = Vec(20, 20);
		pouterSmallDisplay->box.pos = Vec(x - 2.5 , y + 1.f);
		pouterSmallDisplay->fontSize = 26;
		pouterSmallDisplay->textAlign = 18;
		pouterSmallDisplay->textColor = COLOR_COMPUTERSCARE_LIGHT_GREEN;
		pouterSmallDisplay->breakRowWidth = 20;
		pouterSmallDisplay->module = module;


		outputChannelLabel = new SmallLetterDisplay();
		outputChannelLabel->box.size = Vec(5, 5);
		outputChannelLabel->box.pos = Vec(x + labelDx, y - 12 + labelDy);
		outputChannelLabel->fontSize = 14;
		outputChannelLabel->textAlign = index < 8 ? 1 : 4;
		outputChannelLabel->breakRowWidth = 15;

		outputChannelLabel->value = std::to_string(index + 1);

		DisableableSnapKnob* knob =  createParam<DisableableSnapKnob>(Vec(x, y), module, ComputerscareRolyPouter::KNOB + index);
		knob->channel = index;
		knob->module = module;
		addParam(knob);

		addChild(pouterSmallDisplay);
		addChild(outputChannelLabel);

	}
	DisableableSnapKnob* knob;
	PolyOutputChannelsWidget* channelWidget;
	PouterSmallDisplay* pouterSmallDisplay;
	SmallLetterDisplay* outputChannelLabel;

	void addMenuItems(ComputerscareRolyPouter *pouter, Menu *menu);
	void appendContextMenu(Menu *menu) override;
};
struct ssmi : MenuItem
{
	ComputerscareRolyPouter *pouter;
	ComputerscareRolyPouterWidget *pouterWidget;
	int mySetVal = 1;
	ssmi(int setVal)
	{
		mySetVal = setVal;
	}

	void onAction(const event::Action &e) override
	{
		pouter->setAll(mySetVal);
	}
};
void ComputerscareRolyPouterWidget::addMenuItems(ComputerscareRolyPouter *pouter, Menu *menu)
{
	for (int i = 1; i < 17; i++) {
		ssmi *menuItem = new ssmi(i);
		menuItem->text = "Set all to ch. " + std::to_string(i);
		menuItem->pouter = pouter;
		menuItem->pouterWidget = this;
		menu->addChild(menuItem);
	}

}
void ComputerscareRolyPouterWidget::appendContextMenu(Menu *menu)
{
	ComputerscareRolyPouter *pouter = dynamic_cast<ComputerscareRolyPouter *>(this->module);

	MenuLabel *spacerLabel = new MenuLabel();
	menu->addChild(spacerLabel);


	MenuLabel *modeLabel = new MenuLabel();
	modeLabel->text = "Presets";
	menu->addChild(modeLabel);

	addMenuItems(pouter, menu);

}


Model *modelComputerscareRolyPouter = createModel<ComputerscareRolyPouter, ComputerscareRolyPouterWidget>("computerscare-roly-pouter");
