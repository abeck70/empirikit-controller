/*
* Copyright 2016 Devan Lai
* Modifications copyright 2017 Lars Gunder Knudsen
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef WEB_USB_CDC_H
#define WEB_USB_CDC_H

#include "WebUSBDevice.h"

class WebUSBCDC : public WebUSBDevice {
public:
    WebUSBCDC(uint16_t vendor_id, uint16_t product_id, uint16_t product_release = 0x0001, bool connect = true);

protected:
    virtual bool USBCallback_request();
    virtual bool USBCallback_setConfiguration(uint8_t configuration);
    virtual uint8_t * stringIproductDesc();
    virtual uint8_t * stringIinterfaceDesc();
    virtual uint8_t * configurationDesc();
    virtual uint8_t * stringImanufacturerDesc();
    virtual uint8_t * stringIserialDesc();

public:
    bool write(uint8_t * buffer, uint32_t size, bool isCDC=false);
    bool read(uint8_t * buffer, uint32_t * size, bool isCDC=false, bool blocking=false);

    virtual uint8_t * allowedOriginsDesc();
    virtual uint8_t * urlIlandingPage();
    virtual uint8_t * urlIallowedOrigin();

private:
    volatile bool cdc_connected;
};

#endif