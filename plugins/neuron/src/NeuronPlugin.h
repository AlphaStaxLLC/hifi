//
//  NeuronPlugin.h
//  input-plugins/src/input-plugins
//
//  Created by Anthony Thibault on 12/18/2015.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_NeuronPlugin_h
#define hifi_NeuronPlugin_h

#include <controllers/InputDevice.h>
#include <controllers/StandardControls.h>
#include <plugins/InputPlugin.h>

// Handles interaction with the Neuron SDK
class NeuronPlugin : public InputPlugin {
    Q_OBJECT
public:
    // Plugin functions
    virtual bool isSupported() const override;
    virtual bool isJointController() const override { return true; }
    const QString& getName() const override { return NAME; }
    const QString& getID() const override { return NEURON_ID_STRING; }

    virtual void activate() override;
    virtual void deactivate() override;

    virtual void pluginFocusOutEvent() override { _inputDevice->focusOutEvent(); }
    virtual void pluginUpdate(float deltaTime, bool jointsCaptured) override;

    virtual void saveSettings() const override;
    virtual void loadSettings() override;

private:
    class InputDevice : public controller::InputDevice {
    public:
        InputDevice() : controller::InputDevice("Neuron") {}

        // Device functions
        virtual controller::Input::NamedVector getAvailableInputs() const override;
        virtual QString getDefaultMappingConfig() const override;
        virtual void update(float deltaTime, bool jointsCaptured) override;
        virtual void focusOutEvent() override;
    };

    std::shared_ptr<InputDevice> _inputDevice { std::make_shared<InputDevice>() };

    static const QString NAME;
    static const QString NEURON_ID_STRING;
};

#endif // hifi_NeuronPlugin_h

