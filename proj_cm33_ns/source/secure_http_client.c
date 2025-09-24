/*******************************************************************************
* File Name: secure_http_client.c
*
* Description: This file contains the necessary functions to start the HTTPS
* client and send GET, POST, and PUT request to the HTTPS client.
*
* Related Document: See README.md
********************************************************************************
* Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/* Header file includes */
#include "cybsp.h"
#include "cy_secure_sockets.h"
#include "cy_tls.h"

/* Wi-Fi connection manager header files */
#include "cy_wcm.h"
#include "cy_wcm_error.h"

/* Standard C header file */
#include <string.h>

/* HTTPS client task header file. */
#include "secure_http_client.h"
#include "cy_http_client_api.h"
#include "secure_keys.h"
#include "lwip/ip_addr.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>
#include "retarget_io_init.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define LAST_INDEX                                   (1U)
#define MEMSET_VAL                                   (0U)
#define UART_RESULT_SUCCESS                          (1U)
#define APP_SDIO_INTERRUPT_PRIORITY                  (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY             (2U)
#define APP_SDIO_FREQUENCY_HZ                        (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK                      (64U)
#define INITIAL_VALUE                                (0U)

/*******************************************************************************
* Global Variables
********************************************************************************/
static bool get_after_put_flag = false;
static cy_http_client_method_t http_client_method;

/* Holds the security configuration such as client certificate,
 * client key, and rootCA.
 */
static cy_awsport_ssl_credentials_t security_config;

/* Secure HTTP server information. */
static cy_awsport_server_info_t server_info;

/* Buffer to store get response */
static uint8_t http_get_buffer[HTTP_GET_BUFFER_LENGTH];

/* Holds the fields for response header and body */
static cy_http_client_response_t http_response;

/* Holds the IP address obtained using Wi-Fi Connection Manager (WCM). */
static cy_wcm_ip_address_t ip_addr;

/* Secure HTTP client instance. */
static cy_http_client_t https_client;

/* SDIO Instance */
static mtb_hal_sdio_t sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
/* SysPm callback parameter structure for SDHC */
static cy_stc_syspm_callback_params_t sdcardDSParams =
{
        .context   = &sdhc_host_context,
        .base      = CYBSP_WIFI_SDIO_HW
};

/* SysPm callback structure for SDHC*/
static cy_stc_syspm_callback_t sdhcDeepSleepCallbackHandler =
{
    .callback           = Cy_SD_Host_DeepSleepCallback,
    .skipMode           = SYSPM_SKIP_MODE,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &sdcardDSParams,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};
#endif

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static void http_request(void);
static void fetch_https_client_method(void);
static void disconnect_callback_handler(cy_http_client_t handle,
                                 cy_http_client_disconn_type_t type, void *args);
static cy_rslt_t send_http_request(cy_http_client_t handle,
                            cy_http_client_method_t method,const char * pPath);
static cy_rslt_t configure_https_client(void);
static cy_rslt_t wifi_connect(void);

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: sdio_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for SDIO instance.
*******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

/*******************************************************************************
* Function Name: host_wake_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for the host wake up input pin.
*******************************************************************************/
static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

/*******************************************************************************
* Function Name: app_sdio_init
********************************************************************************
* Summary:
* This function configures and initializes the SDIO instance used in
* communication between the host MCU and the wireless device.
*******************************************************************************/
static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;
    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };

    cy_stc_sysint_t host_wake_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    /* Initialize the SDIO interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status = Cy_SysInt_Init(&sdio_intr_cfg,
            sdio_interrupt_handler);

    /* SDIO interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    /* Setup SDIO using the HAL object and desired configuration */
    result = mtb_hal_sdio_setup(&sdio_instance, &CYBSP_WIFI_SDIO_sdio_hal_config,
            NULL, &sdhc_host_context);

    /* SDIO setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Initialize and Enable SD HOST */
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW, CYBSP_WIFI_SDIO_sdio_hal_config.host_config,
            &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);
    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = SDHC_SDIO_64BYTES_BLOCK;

    /* Configure SDIO */
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

    /* Setup GPIO using the HAL object for WIFI WL REG ON  */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin, CYBSP_WIFI_WL_REG_ON_PORT_NUM,
            CYBSP_WIFI_WL_REG_ON_PIN);

    /* Setup GPIO using the HAL object for WIFI HOST WAKE PIN  */
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
            CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    /* Initialize the Host wakeup interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status_host_wake =
            Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler);

    /* Host wake up interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status_host_wake)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
}

/*******************************************************************************
* Function Name: wifi_connect
********************************************************************************
* Summary:
*  The device associates to the Access Point with given SSID, PASSWORD, and
*  SECURITY type. It retries for MAX_WIFI_RETRY_COUNT times if the Wi-Fi
*  connection fails.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the Wi-Fi connection is successfully
*  established, a WCM error code otherwise.
*
*******************************************************************************/
static cy_rslt_t wifi_connect(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t retry_count = INITIAL_VALUE;
    cy_wcm_connect_params_t connect_param = (cy_wcm_connect_params_t)
    {
        .ap_credentials =  {{INITIAL_VALUE}},
        .BSSID =  {INITIAL_VALUE},
        .static_ip_settings = NULL,
        .band =  (cy_wcm_wifi_band_t)INITIAL_VALUE,
        .itwt_profile = CY_WCM_ITWT_PROFILE_NONE
    };

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

    /* SDHC SysPm callback registration */
    Cy_SysPm_RegisterCallback(&sdhcDeepSleepCallbackHandler);

#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */

    /* Initialize SDIO Instance */
    app_sdio_init();

    /* Initialize the Wi-Fi device as a STA.*/
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
    wcm_config.wifi_interface_instance = &sdio_instance;

    /* Initialize WiFi Connection Manager (WCM) */
    result = cy_wcm_init(&wcm_config);

    if (CY_RSLT_SUCCESS == result)
    {
        APP_INFO(("Wi-Fi initialization is successful\n"));
        memcpy(&connect_param.ap_credentials.SSID, WIFI_SSID,
                sizeof(WIFI_SSID));
        memcpy(&connect_param.ap_credentials.password, WIFI_PASSWORD,
                sizeof(WIFI_PASSWORD));
        connect_param.ap_credentials.security = WIFI_SECURITY_TYPE;
        APP_INFO(("Join to AP: %s\n", connect_param.ap_credentials.SSID));

       /* Connect to Access Point. It validates the connection parameters
        * and then establishes connection to AP.
        */
        for (retry_count = INITIAL_VALUE; retry_count < MAX_WIFI_RETRY_COUNT;
                retry_count++)
        {
            result = cy_wcm_connect_ap(&connect_param, &ip_addr);

            if (CY_RSLT_SUCCESS == result)
            {
                APP_INFO(("Successfully joined Wi-Fi network %s\n",
                        connect_param.ap_credentials.SSID));

                if (CY_WCM_IP_VER_V4 == ip_addr.version)
                {
                    APP_INFO(("Assigned IP address: %s\n",
                            ip4addr_ntoa((const ip4_addr_t *)&ip_addr.ip.v4)));
                }
                else if (CY_WCM_IP_VER_V6 == ip_addr.version)
                {
                    APP_INFO(("Assigned IP address: %s\n",
                            ip6addr_ntoa((const ip6_addr_t *)&ip_addr.ip.v6)));
                }

                break;
            }

            ERR_INFO(("Failed to join Wi-Fi network. Retrying...\n"));
        }
    }
    else
    {
        printf("Wi-Fi Connection Manager initialization failed!\n");
        handle_app_error();
    }

    return result;
}

/*******************************************************************************
* Function Name: disconnect_callback
********************************************************************************
* Summary:
*  Callback function for http disconnect.
*******************************************************************************/
static void disconnect_callback_handler(cy_http_client_t handle,
        cy_http_client_disconn_type_t type, void *args)
{
    printf("\nApplication Disconnect callback triggered for handle = "
            "%p type=%d\n", handle, type);
}

/*******************************************************************************
* Function Name: send_http_request
********************************************************************************
* Summary:
*  The function handles an http send operation.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the secure HTTP client is configured
*  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
*
*******************************************************************************/
static cy_rslt_t send_http_request( cy_http_client_t handle,
        cy_http_client_method_t method, const char * pPath)
{
    cy_http_client_request_header_t request;
    cy_http_client_header_t header;
    cy_http_client_response_t response;

    /* Return value of all methods from the HTTP Client library API. */
    cy_rslt_t http_status = CY_RSLT_SUCCESS;

   /* Initialize the response object. The same buffer used for storing
    * request headers is reused here.
    */
    request.buffer = http_get_buffer;
    request.buffer_len = HTTP_GET_BUFFER_LENGTH;
    request.headers_len = HTTP_REQUEST_HEADER_LEN;
    request.method = method;
    request.range_end = HTTP_REQUEST_RANGE_END;
    request.range_start = HTTP_REQUEST_RANGE_START;
    request.resource_path = pPath;
    header.field = "Content-Type";
    header.field_len = sizeof("Content-Type")-LAST_INDEX;
    header.value = "application/x-www-form-urlencoded";
    header.value_len = sizeof("application/x-www-form-urlencoded") - LAST_INDEX;

    http_status = cy_http_client_write_header(handle, &request, &header,
            NUM_HTTP_HEADERS);

    if(CY_RSLT_SUCCESS != http_status)
    {
        printf("\nWrite Header ----------- Fail \n");
    }
    else
    {
        printf( "\n Sending Request Headers:\n%.*s\n",
                ( int ) request.headers_len, ( char * ) request.buffer);
        http_status = cy_http_client_send(handle, &request,
                (uint8_t *)REQUEST_BODY, REQUEST_BODY_LENGTH, &response);

        if(CY_RSLT_SUCCESS != http_status)
        {
            printf("\nFailed to send HTTP method=%d\n Error=%ld\r\n",
                    request.method,(unsigned long)http_status);
        }
        else
        {
            if ( CY_HTTP_CLIENT_METHOD_HEAD != method )
            {
                TEST_INFO(( "Received HTTP response from %.*s%.*s...\n"
                       "Response Headers:\n %.*s\n"
                       "Response Status :\n %u \n"
                       "Response Body   :\n %.*s\n",
                       ( int ) sizeof(HTTPS_SERVER_HOST)-LAST_INDEX,
                       HTTPS_SERVER_HOST,
                       ( int ) sizeof(request.resource_path) -LAST_INDEX,
                       request.resource_path,
                       ( int ) response.headers_len, response.header,
                       response.status_code,
                       ( int ) response.body_len, response.body ));

            }
            printf("\n buffer_len:[%d] headers_len:[%d] header_count:"
                    "[%d] body_len:[%d] content_len:[%d]\n",
                     response.buffer_len, response.headers_len,
                     response.header_count, response.body_len,
                     response.content_len);
        }
    }

    return http_status;
}

/*******************************************************************************
* Function Name: configure_https_client
********************************************************************************
* Summary:
*  Configures the security parameters such as client certificate, private key,
*  and the root CA certificate to start the HTTP client in secure mode.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the secure HTTP client is configured
*  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
*
*******************************************************************************/
static cy_rslt_t configure_https_client(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_http_disconnect_callback_t http_cb;
    ( void ) memset( &security_config, MEMSET_VAL, sizeof( security_config ) );
    ( void ) memset( &server_info, MEMSET_VAL, sizeof( server_info ) );

    /* Set the credential information. */
    security_config.client_cert      = (const char *) &keyCLIENT_CERTIFICATE_PEM;
    security_config.client_cert_size = sizeof( keyCLIENT_CERTIFICATE_PEM );
    security_config.private_key      = (const char *) &keyCLIENT_PRIVATE_KEY_PEM;
    security_config.private_key_size = sizeof( keyCLIENT_PRIVATE_KEY_PEM );
    security_config.root_ca          = (const char *) &keySERVER_ROOTCA_PEM;
    security_config.root_ca_size     = sizeof( keySERVER_ROOTCA_PEM );
    server_info.host_name = HTTPS_SERVER_HOST;
    server_info.port = HTTPS_PORT;

    /* Initialize the HTTP Client Library. */
    result = cy_http_client_init();

    if(CY_RSLT_SUCCESS != result)
    {
        /* Failure path. */
        ERR_INFO(("Failed to initialize http client.\n"));
    }
    http_cb = disconnect_callback_handler;

    /* Create an instance of the HTTP client. */
    result = cy_http_client_create(&security_config, &server_info, http_cb,
            NULL, &https_client);
    
    if(CY_RSLT_SUCCESS != result)
    {
        /* Failure path */
        ERR_INFO(("Failed to create http client.\n"));
    }

    return result;
}

/*******************************************************************************
* Function Name: https_client_task
********************************************************************************
* Summary:
*  Starts the HTTP client in secure mode. This example application is using a
*  self-signed certificate which means there is no third-party certificate issuing
*  authority involved in the authentication of the client. It is the user's
*  responsibility to supply the necessary security configurations such as client's
*  certificate, private key of the client, and RootCA of the client to start the
*  HTTP client in secure mode.
*
* Parameters:
*  arg - Unused.
*
* Return:
*  None.
*
*******************************************************************************/
void https_client_task(void *arg)
{
    cy_rslt_t result = CY_RSLT_TYPE_ERROR;
    CY_UNUSED_PARAMETER(arg);

    /* Connects to the Wi-Fi Access Point. */
    result = wifi_connect();
    PRINT_AND_ASSERT(result, "Wi-Fi connection failed.\n");

   /* Configure the HTTPS client with all the security parameters and
    * register a default dynamic URL handler.
    */
    result = configure_https_client();
    PRINT_AND_ASSERT(result, "Failed to configure the HTTPS client.\n");

    /* Connect the HTTP client to server. */
    result = cy_http_client_connect(https_client, TRANSPORT_SEND_RECV_TIMEOUT_MS,
                                     TRANSPORT_SEND_RECV_TIMEOUT_MS);

    if(CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to connect to the http server.\n"));
    }
    else
    {
        printf("Successfully connected to http server\r\n");

        while(true)
        {
            /*fetch HTTP client Methods. */
            fetch_https_client_method();
        }
    }
}

/*******************************************************************************
* Function Name: fetch_https_client_method
********************************************************************************
* Summary:
*  The function handles an http methods.
*******************************************************************************/
static void fetch_https_client_method(void)
{

    /* Uart read variable */
    uint8_t uart_read_value;
    uint8_t uart_result;

    /* Options to select the method*/
    printf("\n===============================================================\n");
    printf(MENU_HTTPS_METHOD);
    printf("\n===============================================================\n");

    /* Reading option number from console */
    uart_result=scanf("%hhu", &uart_read_value);

    if(UART_RESULT_SUCCESS != uart_result)
    {
        printf("Failed to read input value");
    }

    switch(uart_read_value)
    {
         case HTTPS_GET_METHOD:
         {
             printf("\n HTTP GET Request..\n");
             http_client_method = CY_HTTP_CLIENT_METHOD_GET;

            /* Send the HTTP request and body to the server, and receive
             * the status from it.
             */
             http_request();
             break;
         }
         case HTTPS_POST_METHOD:
         {
             printf("\n HTTP POST Request..\n");
             http_client_method = CY_HTTP_CLIENT_METHOD_POST;

            /* Send the HTTP request and body to the server, and receive the
             * status from it.
             */
             http_request();
             break;
         }
         case HTTPS_PUT_METHOD:
         {
             printf("\n HTTP PUT Request..\n");
             http_client_method = CY_HTTP_CLIENT_METHOD_PUT;

            /* Send the HTTP request and body to the server, and receive the
             * status from it.
             */
             http_request();
             break;
         }
         case HTTPS_GET_METHOD_AFTER_PUT:
         {
             printf("\n HTTP GET FOR PUT Request..\n");
             http_client_method = CY_HTTP_CLIENT_METHOD_GET;
             get_after_put_flag = true;

            /* Send the HTTP request and body to the server, and receive the
             * status from it.
             */
             http_request();
             break;
         }
        default:
        {
            printf("\x1b[2J\x1b[;H");
            printf("\r\nPlease select from the given valid options\r\n");
            break;
        }
    }
}

/*******************************************************************************
* Function Name: http_request
********************************************************************************
* Summary:
*  The function handles an http request operation.
*******************************************************************************/
static void http_request(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

   /* Send the HTTP request and body to the server, and receive the response
    * from it.
    */
    if(get_after_put_flag)
    {
        get_after_put_flag = false;
        result = send_http_request(https_client, http_client_method,
                HTTP_GET_PATH_AFTER_PUT);
    }
    else
    {
        result = send_http_request(https_client, http_client_method, HTTP_PATH);
    }

    if(CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to send the http request.\n"));
    }
    else
    {
        printf("\r\n Successfully sent GET request to http server\r\n");
        printf("\r\n The http status code is :: %d\r\n",
                 http_response.status_code);
    }
}


/* [] END OF FILE */
