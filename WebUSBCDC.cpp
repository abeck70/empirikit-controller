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
*
*/

#include "stdint.h"

#include "USBHAL.h"
#include "WebUSBCDC.h"
#include "WebUSB.h"
#include "WinUSB.h"

#include "USBDescriptor.h"

static uint8_t cdc_line_coding[7]= {0x80, 0x25, 0x00, 0x00, 0x00, 0x00, 0x08};

#define DEFAULT_CONFIGURATION (1)

#define CDC_SET_LINE_CODING        0x20
#define CDC_GET_LINE_CODING        0x21
#define CDC_SET_CONTROL_LINE_STATE 0x22

// Control Line State bits
#define CLS_DTR   (1 << 0)
#define CLS_RTS   (1 << 1)

#define CDC_INT_INTERFACE_NUMBER  (0)
#define CDC_INTERFACE_NUMBER  (1)
#define WEBUSB_INTERFACE_NUMBER  (2)

#define MAX_CDC_REPORT_SIZE MAX_PACKET_SIZE_EPBULK

// Descriptor defines (in addition to those in USBDescriptor.h)
#define USB_VERSION_1_1     (0x0110)

#define IAD_DESCRIPTOR                                  (0x0b)
#define HEADER_FUNCTIONAL_DESCRIPTOR                    (0x00)
#define CALL_MANAGEMENT_FUNCTIONAL_DESCRIPTOR           (0x01)
#define ACM_FUNCTIONAL_DESCRIPTOR                       (0x02)
#define UNION_FUNCTIONAL_DESCRIPTOR                     (0x06)

#define IAD_DESCRIPTOR_LENGTH                           (8)
#define HEADER_FUNCTIONAL_DESCRIPTOR_LENGTH             (5)
#define CALL_MANAGEMENT_FUNCTIONAL_DESCRIPTOR_LENGTH    (5)
#define ACM_FUNCTIONAL_DESCRIPTOR_LENGTH                (4)
#define UNION_FUNCTIONAL_DESCRIPTOR_LENGTH              (5)

#define IAD_INTERFACE_COUNT                             (2)
#define CS_INTERFACE                                    (0x24)
#define CS_ENDPOINT                                     (0x25)

#define CDC_CLASS                                       (0x02)
#define CDC_CLASS_DATA                                  (0x0A)
#define CUSTOM_CLASS                                    (0xFF)

#define ACM_SUBCLASS                                    (0x02)

#define CDC_INTERFACE_COUNT                             (2)
#define CDC_INT_ENDPOINT_COUNT                          (1)
#define CDC_DATA_ENDPOINT_COUNT                         (2)
#define CDC_ENDPOINT_INT                                (EPINT_IN)
#define CDC_ENDPOINT_IN                                 (EPBULK_IN)
#define CDC_ENDPOINT_OUT                                (EPBULK_OUT)
#define CDC_EPINT_INTERVAL                              (16)

#define WEBUSB_INTERFACE_COUNT                          (1)
#define WEBUSB_ENDPOINT_COUNT                           (2)
#define WEBUSB_ENDPOINT_IN                              (EP5IN)
#define WEBUSB_ENDPOINT_OUT                             (EP5OUT)


WebUSBCDC::WebUSBCDC(uint16_t vendor_id, uint16_t product_id, uint16_t product_release, bool connect)
    :  WebUSBDevice(vendor_id, product_id, product_release)
{
    cdc_connected = false;
    if (connect) {
        WebUSBDevice::connect();
    }
}

char endl_str[] = "\r\n";

#ifdef __DEBUG
Serial pc(USBTX, USBRX); // tx, rx
#endif

bool WebUSBCDC::USBCallback_request() {
    bool success = false;

    CONTROL_TRANSFER * transfer = getTransferPtr();

#ifdef __DEBUG
    if(transfer->setup.bRequest == WINUSB_VENDOR_CODE) {
        pc.printf("Type: %02x\r\n",transfer->setup.bmRequestType.Type);
        pc.printf("Recipient: %02x\r\n",transfer->setup.bmRequestType.Recipient);
        pc.printf("bRequest: %02x\r\n",transfer->setup.bRequest);
        pc.printf("wIndex: %04x\r\n",transfer->setup.wIndex);
        pc.printf("wValue: %04x\r\n\r\n",transfer->setup.wValue);
    }
#endif

    // Handle the Microsoft OS Descriptors 1.0 special string descriptor request
    if ((transfer->setup.bmRequestType.Type == STANDARD_TYPE) &&
        (transfer->setup.bRequest == GET_DESCRIPTOR) &&
        (DESCRIPTOR_TYPE(transfer->setup.wValue) == STRING_DESCRIPTOR) &&
        (DESCRIPTOR_INDEX(transfer->setup.wValue) == 0xEE))
    {
        static uint8_t msftStringDescriptor[] = {
            0x12,                                      /* bLength */
            STRING_DESCRIPTOR,                         /* bDescriptorType */
            'M',0,'S',0,'F',0,'T',0,'1',0,'0',0,'0',0, /* qWSignature - MSFT100 */
            WINUSB_VENDOR_CODE,                        /* bMS_VendorCode */
            0x00,                                      /* bPad */
        };

        transfer->remaining = msftStringDescriptor[0];
        transfer->ptr = msftStringDescriptor;
        transfer->direction = DEVICE_TO_HOST;
        success = true;
    }

    // Process Microsoft OS Descriptors 1.0 Compatible ID requests
    else if ((transfer->setup.bmRequestType.Type == VENDOR_TYPE) &&
             (transfer->setup.bmRequestType.Recipient == DEVICE_RECIPIENT) &&
             (transfer->setup.bRequest == WINUSB_VENDOR_CODE) &&
             (transfer->setup.wIndex == WINUSB_GET_COMPATIBLE_ID_FEATURE_DESCRIPTOR))
    {
        static uint8_t msftCompatibleIdDescriptor[] = {
            0x28, 0x00, 0x00, 0x00,         /* dwLength */
            LSB(COMPATIBLE_ID_VERSION_1_0), /* bcdVersion (LSB) */
            MSB(COMPATIBLE_ID_VERSION_1_0), /* bcdVersion (MSB) */
            LSB(WINUSB_GET_COMPATIBLE_ID_FEATURE_DESCRIPTOR), /* wIndex (LSB) */
            MSB(WINUSB_GET_COMPATIBLE_ID_FEATURE_DESCRIPTOR), /* wIndex (MSB) */
            0x01,
            0, 0, 0, 0, 0, 0, 0,            /* reserved */
            WEBUSB_INTERFACE_NUMBER,        /* bFirstInterfaceNumber */
            0x00,                           /* reserved */
            'W','I','N','U','S','B',0,0,    /* compatible ID - WINUSB */
            0, 0, 0, 0, 0, 0, 0, 0,         /* subCompatibleID */
            0, 0, 0, 0, 0, 0,               /* reserved */
        };

        transfer->remaining = sizeof(msftCompatibleIdDescriptor);
        transfer->ptr = msftCompatibleIdDescriptor;
        transfer->direction = DEVICE_TO_HOST;
        success = true;
    }

    // Process Microsoft OS Descriptors 1.0 Extended Properties ID requests
    // BEWARE: The following descriptor is work in progress - doesn't seem to be 100% correct yet.
    else if ((transfer->setup.bmRequestType.Type == VENDOR_TYPE) &&
             (transfer->setup.bmRequestType.Recipient == INTERFACE_RECIPIENT) &&
             (transfer->setup.bRequest == WINUSB_VENDOR_CODE) &&
             //(transfer->setup.wValue == WEBUSB_INTERFACE_NUMBER) &&
             ((transfer->setup.wIndex == WINUSB_GET_EXTENDED_PROPERTIES_OS_FEATURE_DESCRIPTOR) ||
              (transfer->setup.wIndex == WEBUSB_INTERFACE_NUMBER)))
    {
        static uint8_t msftExtendedPropertiesDescriptor[] = {
            0x8c, 0x00, 0x00, 0x00,         /* dwLength */
            LSB(COMPATIBLE_ID_VERSION_1_0), /* bcdVersion (LSB) */
            MSB(COMPATIBLE_ID_VERSION_1_0), /* bcdVersion (MSB) */
            LSB(WINUSB_GET_EXTENDED_PROPERTIES_OS_FEATURE_DESCRIPTOR), /* wIndex (LSB) */
            MSB(WINUSB_GET_EXTENDED_PROPERTIES_OS_FEATURE_DESCRIPTOR), /* wIndex (MSB) */
            0x01, 0x00,                     // Number of sections
            0x82, 0x00, 0x00, 0x00,         // Size of property section
            0x01, 0x00, 0x00, 0x00,         // Data type (1 = REG_SZ)
            0x28, 0x00,                     // property name length (40)
            'D',0,'e',0,'v',0,'i',0,'c',0,'e',0,'I',0,'n',0,'t',0,'e',0,'r',0,'f',0,'a',0,'c',0,'e',0,'G',0,'U',0,'I',0,'D',0,0,0,
            0x4e, 0x00, 0x00, 0x00,         // property data length (78)
            '{',0,'F',0,'3',0,'5',0,'E',0,'1',0,'B',0,'9',0,
            'F',0,'-',0,'9',0,'E',0,'F',0,'1',0,'-',0,'4',0,
            'D',0,'7',0,'1',0,'-',0,'9',0,'9',0,'D',0,'C',0,
            '-',0,'B',0,'1',0,'C',0,'B',0,'C',0,'8',0,'0',0,
            'E',0,'C',0,'1',0,'4',0,'3',0,'}',0,  0,0,
        };

        transfer->remaining = sizeof(msftExtendedPropertiesDescriptor);
        transfer->ptr = msftExtendedPropertiesDescriptor;
        transfer->direction = DEVICE_TO_HOST;
        success = true;
    }
    // Process CDC class-specific requests
    if (transfer->setup.bmRequestType.Type == CLASS_TYPE) {
        switch (transfer->setup.bRequest) {
            case CDC_GET_LINE_CODING:
                transfer->remaining = 7;
                transfer->ptr = cdc_line_coding;
                transfer->direction = DEVICE_TO_HOST;
                success = true;
                break;
            case CDC_SET_LINE_CODING:
                transfer->remaining = 7;
                transfer->notify = true;
                success = true;
                break;
            case CDC_SET_CONTROL_LINE_STATE:
                // we should handle this specifically for the CDC endpoint.
                if (transfer->setup.wValue & CLS_DTR) {
                    cdc_connected = true;
                } else {
                    cdc_connected = false;
                }
                success = true;
                break;
            default:
                break;
        }
    }

    // Process WebUSB vendor requests
    if (!success)
    {
        success = WebUSBDevice::USBCallback_request();
    }

    return success;
}

// Called in ISR context
// Set configuration. Return false if the
// configuration is not supported.
bool WebUSBCDC::USBCallback_setConfiguration(uint8_t configuration) {
    if (configuration != DEFAULT_CONFIGURATION) {
        return false;
    }

    addEndpoint(EPINT_IN, MAX_PACKET_SIZE_EPINT);

    addEndpoint(EPBULK_IN, MAX_PACKET_SIZE_EPBULK);
    addEndpoint(EPBULK_OUT, MAX_PACKET_SIZE_EPBULK);

    addEndpoint(WEBUSB_ENDPOINT_IN, MAX_PACKET_SIZE_EPBULK);
    addEndpoint(WEBUSB_ENDPOINT_OUT, MAX_PACKET_SIZE_EPBULK);

    // We activate the endpoints to be able to recceive data
    readStart(EPBULK_OUT, MAX_PACKET_SIZE_EPBULK);
    readStart(WEBUSB_ENDPOINT_OUT, MAX_PACKET_SIZE_EPBULK);
    return true;
}

bool WebUSBCDC::write(uint8_t * buffer, uint32_t size, bool isCDC) {
    if(isCDC && !cdc_connected)
        return false;

    return USBDevice::write(isCDC ? CDC_ENDPOINT_IN : WEBUSB_ENDPOINT_IN, buffer, size, MAX_CDC_REPORT_SIZE);
}

bool WebUSBCDC::read(uint8_t * buffer, uint32_t * size, bool isCDC, bool blocking) {
    if(isCDC && !cdc_connected)
        return false;

    if (blocking && !USBDevice::readEP(isCDC ? CDC_ENDPOINT_OUT : WEBUSB_ENDPOINT_OUT, buffer, size, MAX_CDC_REPORT_SIZE))
        return false;
    if (!blocking && !USBDevice::readEP_NB(isCDC ? CDC_ENDPOINT_OUT : WEBUSB_ENDPOINT_OUT, buffer, size, MAX_CDC_REPORT_SIZE))
        return false;
    if (!readStart(isCDC ? CDC_ENDPOINT_OUT : WEBUSB_ENDPOINT_OUT, MAX_CDC_REPORT_SIZE))
        return false;

    return true;
}

#define FULL_CONFIGURATION_SIZE   (CONFIGURATION_DESCRIPTOR_LENGTH + \
    (3 * INTERFACE_DESCRIPTOR_LENGTH) + (5 * ENDPOINT_DESCRIPTOR_LENGTH) + \
    IAD_DESCRIPTOR_LENGTH + HEADER_FUNCTIONAL_DESCRIPTOR_LENGTH + CALL_MANAGEMENT_FUNCTIONAL_DESCRIPTOR_LENGTH + \
    ACM_FUNCTIONAL_DESCRIPTOR_LENGTH + UNION_FUNCTIONAL_DESCRIPTOR_LENGTH)

uint8_t * WebUSBCDC::configurationDesc() {
    static uint8_t configDescriptor[] = {
        // configuration descriptor
        CONFIGURATION_DESCRIPTOR_LENGTH,
        CONFIGURATION_DESCRIPTOR,
        LSB(FULL_CONFIGURATION_SIZE),
        MSB(FULL_CONFIGURATION_SIZE),
        CDC_INTERFACE_COUNT+WEBUSB_INTERFACE_COUNT,
        0x01,
        0x00,
        C_RESERVED,
        C_POWER(100),

        // IAD to associate the two CDC interfaces (this seems to be needed by Windows)
        IAD_DESCRIPTOR_LENGTH,
        IAD_DESCRIPTOR,
        CDC_INT_INTERFACE_NUMBER,
        IAD_INTERFACE_COUNT,
        CDC_CLASS,
        ACM_SUBCLASS,
        0,
        0,

        // CDC BLOCK STARTS

        // CDC INTERRUPT INTERFACE
        INTERFACE_DESCRIPTOR_LENGTH,
        INTERFACE_DESCRIPTOR,
        CDC_INT_INTERFACE_NUMBER,
        0x00,
        CDC_INT_ENDPOINT_COUNT,
        CDC_CLASS,
        ACM_SUBCLASS,
        0x01,
        0x00,

        // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
        HEADER_FUNCTIONAL_DESCRIPTOR_LENGTH,
        CS_INTERFACE,
        HEADER_FUNCTIONAL_DESCRIPTOR,
        LSB(USB_VERSION_1_1),
        MSB(USB_VERSION_1_1),

        // CDC Call Management Functional Descriptor,
        CALL_MANAGEMENT_FUNCTIONAL_DESCRIPTOR_LENGTH,
        CS_INTERFACE,
        CALL_MANAGEMENT_FUNCTIONAL_DESCRIPTOR,
        0x03,
        CDC_INTERFACE_NUMBER,

        // CDC Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
        ACM_FUNCTIONAL_DESCRIPTOR_LENGTH,
        CS_INTERFACE,
        ACM_FUNCTIONAL_DESCRIPTOR,
        0x02,

        // CDC Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
        UNION_FUNCTIONAL_DESCRIPTOR_LENGTH,
        CS_INTERFACE,
        UNION_FUNCTIONAL_DESCRIPTOR,
        CDC_INT_INTERFACE_NUMBER,
        CDC_INTERFACE_NUMBER,

        // CDC INT EP
        ENDPOINT_DESCRIPTOR_LENGTH,
        ENDPOINT_DESCRIPTOR,
        PHY_TO_DESC(EPINT_IN),
        E_INTERRUPT,
        LSB(MAX_PACKET_SIZE_EPINT),
        MSB(MAX_PACKET_SIZE_EPINT),
        CDC_EPINT_INTERVAL,

        // CDC DATA INTERFACE
        INTERFACE_DESCRIPTOR_LENGTH,
        INTERFACE_DESCRIPTOR,
        CDC_INTERFACE_NUMBER,
        0x00,
        CDC_DATA_ENDPOINT_COUNT,
        CDC_CLASS_DATA,
        0x00,
        0x00,
        0x00,

        // CDC DATA ENDPOINT IN
        ENDPOINT_DESCRIPTOR_LENGTH,
        ENDPOINT_DESCRIPTOR,
        PHY_TO_DESC(EPBULK_IN),
        E_BULK,
        LSB(MAX_PACKET_SIZE_EPBULK),
        MSB(MAX_PACKET_SIZE_EPBULK),
        0x00,

        // CDC DATA ENDPOINT OUT
        ENDPOINT_DESCRIPTOR_LENGTH,
        ENDPOINT_DESCRIPTOR,
        PHY_TO_DESC(EPBULK_OUT),
        E_BULK,
        LSB(MAX_PACKET_SIZE_EPBULK),
        MSB(MAX_PACKET_SIZE_EPBULK),
        0x00,


        // WEBUSB BLOCK

        // WEBUSB INTERFACE
        INTERFACE_DESCRIPTOR_LENGTH,
        INTERFACE_DESCRIPTOR,
        WEBUSB_INTERFACE_NUMBER,
        0x00,
        WEBUSB_ENDPOINT_COUNT,
        CUSTOM_CLASS,
        0x00,
        0x00,
        0x00,

        // WEBUSB ENDPOINT IN
        ENDPOINT_DESCRIPTOR_LENGTH,
        ENDPOINT_DESCRIPTOR,
        PHY_TO_DESC(WEBUSB_ENDPOINT_IN),
        E_BULK,
        LSB(MAX_PACKET_SIZE_EPBULK),
        MSB(MAX_PACKET_SIZE_EPBULK),
        0x00,

        // WEBUSB ENDPOINT OUT
        ENDPOINT_DESCRIPTOR_LENGTH,
        ENDPOINT_DESCRIPTOR,
        PHY_TO_DESC(WEBUSB_ENDPOINT_OUT),
        E_BULK,
        LSB(MAX_PACKET_SIZE_EPBULK),
        MSB(MAX_PACKET_SIZE_EPBULK),
        0x00,

    };
    return configDescriptor;
}

// TODO: Make the following dynamic - hardcoded right now

uint8_t * WebUSBCDC::stringIinterfaceDesc() {
    static uint8_t stringIinterfaceDescriptor[] = {
        0x08,
        STRING_DESCRIPTOR,
        'C',0,'D',0,'C',0,
    };
    return stringIinterfaceDescriptor;
}

uint8_t * WebUSBCDC::stringIproductDesc() {
    static uint8_t stringIproductDescriptor[] = {
        0x22,
        STRING_DESCRIPTOR,
        'e',0,'m',0,'p',0,'i',0,'r',0,'i',0,'K',0,'i',0,'t',0,'|',0,'M',0,'O',0,'T',0,'I',0,'O',0,'N',0,
    };
    return stringIproductDescriptor;
}

uint8_t * WebUSBCDC::stringImanufacturerDesc() {
    static uint8_t stringImanufacturerDescriptor[] = {
        0x14,                                            /*bLength*/
        STRING_DESCRIPTOR,                               /*bDescriptorType 0x03*/
        'e',0,'m',0,'p',0,'i',0,'r',0,'i',0,'K',0,'i',0,'t',0,
    };
    return stringImanufacturerDescriptor;
}

uint8_t * WebUSBCDC::stringIserialDesc() {
    static uint8_t stringIserialDescriptor[] = {
        0x0C,                                             /*bLength*/
        STRING_DESCRIPTOR,                                /*bDescriptorType 0x03*/
        '0',0,'0',0,'0',0,'0',0,'1',0,                    /*bString iSerial - 00001*/
    };
    return stringIserialDescriptor;
}

#if 0
uint8_t * WebUSBCDC::urlIlandingPage() {
    static uint8_t urlIlandingPageDescriptor[] = {
        0x11,                  /* bLength */
        WEBUSB_URL,            /* bDescriptorType */
        WEBUSB_URL_SCHEME_HTTP,/* bScheme */
        'l','o','c','a','l','h','o','s','t',':','8','0','0','0', /* URL - localhost:8000 */
    };
    return urlIlandingPageDescriptor;
}
#else
uint8_t * WebUSBCDC::urlIlandingPage() {
    static uint8_t urlIlandingPageDescriptor[] = {
        0x16,                  /* bLength */
        WEBUSB_URL,            /* bDescriptorType */
        WEBUSB_URL_SCHEME_HTTPS,/* bScheme */
        'e','m','p','i','r','i','k','i','t','.','g','i','t','h','u','b','.','i','o',
    };
    return urlIlandingPageDescriptor;
}
#endif

// Deprecated: to be removed when in stable chrome
#define NUM_ORIGINS 1
#define TOTAL_ORIGINS_LENGTH (WEBUSB_DESCRIPTOR_SET_LENGTH + \
                              WEBUSB_CONFIGURATION_SUBSET_LENGTH + \
                              WEBUSB_FUNCTION_SUBSET_LENGTH + \
                              NUM_ORIGINS)

uint8_t * WebUSBCDC::allowedOriginsDesc() {
    static uint8_t allowedOriginsDescriptor[] = {
        WEBUSB_DESCRIPTOR_SET_LENGTH,   /* bLength */
        WEBUSB_DESCRIPTOR_SET_HEADER,   /* bDescriptorType */
        LSB(TOTAL_ORIGINS_LENGTH),      /* wTotalLength (LSB) */
        MSB(TOTAL_ORIGINS_LENGTH),      /* wTotalLength (MSB) */
        0x01,                           /* bNumConfigurations */

        WEBUSB_CONFIGURATION_SUBSET_LENGTH, /* bLength */
        WEBUSB_CONFIGURATION_SUBSET_HEADER, /* bDescriptorType */
        DEFAULT_CONFIGURATION,          /* bConfigurationValue */
        0x01,                           /* bNumFunctions */

        (WEBUSB_FUNCTION_SUBSET_LENGTH+NUM_ORIGINS),/* bLength */
        WEBUSB_FUNCTION_SUBSET_HEADER,  /* bDescriptorType */
        WEBUSB_INTERFACE_NUMBER,        /* bFirstInterfaceNumber */
        URL_OFFSET_ALLOWED_ORIGIN,      /* iOrigin[] */
    };

    return allowedOriginsDescriptor;
}

uint8_t * WebUSBCDC::urlIallowedOrigin() {
    static uint8_t urlIallowedOriginDescriptor[] = {
        0x11,                  /* bLength */
        WEBUSB_URL,            /* bDescriptorType */
        WEBUSB_URL_SCHEME_HTTP,/* bScheme */
        'l','o','c','a','l','h','o','s','t',':','8','0','0','0', /* URL - localhost:8000 */
    };
    return urlIallowedOriginDescriptor;
}

