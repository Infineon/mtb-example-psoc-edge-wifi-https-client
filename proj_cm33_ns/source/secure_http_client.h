/*******************************************************************************
* File Name: secure_http_client.h
*
* Description: This file contains configuration parameters for the secure HTTP
* client along with the HTML page that the client will host.
*
* Related Document: See README.md
********************************************************************************
* (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Include guard
*******************************************************************************/
#ifndef SECURE_HTTP_CLIENT_H_
#define SECURE_HTTP_CLIENT_H_

/* FreeRTOS header file */
#include "FreeRTOS.h"
#include <task.h>
#include "cy_wcm.h"
#include "cybsp.h"
#include "cy_network_mw_core.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define TEST_INFO( x )                           (printf x)

/* Wi-Fi Credentials: Modify WIFI_SSID and WIFI_PASSWORD to match your Wi-Fi
 * network Credentials.
 */
#define WIFI_SSID                                "MY_WIFI_SSID"
#define WIFI_PASSWORD                            "MY_WIFI_PASSWORD"

/* Security type of the Wi-Fi access point. See 'cy_wcm_security_t' structure
 * in "cy_wcm.h" for more details.
 */
#define WIFI_SECURITY_TYPE                       CY_WCM_SECURITY_WPA2_AES_PSK
#define MAX_WIFI_RETRY_COUNT                     (3U)
#define APP_INFO(x)                do { printf("Info: "); printf x; } while(0);
#define ERR_INFO(x)                do { printf("Error: "); printf x; } while(0);
#define PRINT_AND_ASSERT(result, msg, args...)   \
                                     do                                 \
                                     {                                  \
                                         if (CY_RSLT_SUCCESS != result) \
                                         {                              \
                                             ERR_INFO((msg, ## args));  \
                                             CY_ASSERT(0);              \
                                         }                              \
                                     } while(0);
#define HTTPS_PORT                               (50007U)
#define HTTPS_SERVER_HOST                        "172.20.10.3"
#define TRANSPORT_SEND_RECV_TIMEOUT_MS           (5000U)
#define HTTP_GET_BUFFER_LENGTH                   (2048U)
#define REQUEST_BODY                             "/myhellomessage=Hello!"
#define HTTP_PATH                                "/"
#define HTTP_GET_PATH_AFTER_PUT                  "/myhellomessage"
#define REQUEST_BODY_LENGTH                      ( sizeof( REQUEST_BODY ) - 1U )

/* Wi-Fi re-connection time interval in milliseconds */
#define WIFI_CONN_RETRY_INTERVAL_MSEC            (1000U)

/*End Range until where the data is expected. Set Set this to -1 if requested
 * range is all bytes from the starting
 */
#define HTTP_REQUEST_RANGE_END                   (-1)

/* Start Range from where the server should return. */
#define HTTP_REQUEST_RANGE_START                 (0U)

/* Number of headers in the header list */
#define NUM_HTTP_HEADERS                         (1U)

/* Length of the request header. */
#define HTTP_REQUEST_HEADER_LEN                  (0U)

/* HTTPS Menu for options to select the method from keyboard */
#define MENU_HTTPS_METHOD                                                      \
        "\n"                                                                   \
        "Please select the index of HTTP method to be tested from below:\n"    \
        "\n"                                                                   \
        "1. HTTPS_GET_METHOD\n"                                                \
        "2. HTTPS_POST_METHOD\n"                                               \
        "3. HTTPS_PUT_METHOD\n"                                                \
        "4. HTTPS_GET_METHOD_AFTER_PUT\n"                                      \

/*******************************************************************************
* Enumerations
*******************************************************************************/

/*  HTTP Client supported methods  */
typedef enum
{
    HTTPS_GET_METHOD = 1,
    HTTPS_POST_METHOD,
    HTTPS_PUT_METHOD,
    HTTPS_GET_METHOD_AFTER_PUT,
} https_menu_t;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
void https_client_task(void *arg);

#endif /* SECURE_HTTP_CLIENT_H_ */


/* [] END OF FILE */
