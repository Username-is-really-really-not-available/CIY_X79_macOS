#pragma once

#include "RGBController.h"
#include "CIYController.h"

class RGBController_CIYX79 : public RGBController
{
public:
    RGBController_CIYX79(CIYController* controller_ptr);
    ~RGBController_CIYX79();

    void SetupZones() override;
    void DeviceUpdateLEDs() override;
    void DeviceUpdateMode() override;

private:
    CIYController* controller;
    std::vector<int> led_hardware_indices;
};
