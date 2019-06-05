/*
 * Copyright (C) 2019 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_sock_dtls    DTLS sock API
 * @ingroup     net_sock
 * @brief       Sock submodule for DTLS
 *
 * DTLS sock acts as a wrapper for the underlying DTLS module to provide
 * encryption for application using the UDP sock API.
 *
 * How To Use
 * ----------
 *
 * ### Summary
 *
 * - Include module implementing the DTLS sock API in the Makefile
 * - Add credentials
 *   1. Fill credman_credential_t with the credential information
 *   2. Add the credential using @ref credman_add()
 * - Server operation
 *   1. Create UDP sock @ref sock_udp_create()
 *   2. Create DTLS sock @ref sock_dtls_create()
 *   3. Init DTLS server @ref sock_dtls_init_server()
 *   4. Start listening with @ref sock_dtls_recv()
 * - Client operation
 *   1. Create UDP sock @ref sock_udp_create()
 *   2. Create DTLS sock @ref sock_dtls_create()
 *   3. Establish session to server @ref sock_dtls_establish_session()
 *   4. Send packet to server @ref sock_dtls_send()
 *
 * ## Makefile Includes
 *
 * First we need to @ref including-modules "include" a module that implements
 * this API in our applications Makefile. For example the implementation for
 * @ref pkg_tinydtls "tinydtls" is called `tinydtls_sock_dtls'.
 *
 * The corresponding pkg providing the DTLS implementation will be
 * automatically included so there is no need to use `USEPKG` to add the pkg
 * manually.
 *
 * Each DTLS implementation may have their own configuration options and caveat.
 * This can be found at @ref net_dtls "DTLS"
 *
 * ### Adding credentials
 *
 * Before using this module, either as a server or a client, we need to first
 * add the credentials to be used for the encryption using
 * @ref net_credman "credman". Note that credman does not copies the credentials
 * given into the system, it only have information about the credentials and where
 * it is located at. So it is your responsibility to make sure that the credential
 * is valid during the lifetime of your application.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * #include <stdio.h>
 *
 * #include "net/credman.h"
 *
 * static uint8_t psk_key = "secretPSK"
 * static ecdsa_public_key_t other_pubkeys[] = {
 *     { .x = client_pubkey_x, .y = client_pubkey_y },
 * };
 *
 * static const unsigned char server_ecdsa_priv_key[] = {...};
 * static const unsigned char server_ecdsa_pub_key_x[] = {...};
 * static const unsigned char server_ecdsa_pub_key_y[] = {...};
 * static const unsigned char client_pubkey_x[] = {...};
 * static const unsigned char client_pubkey_y[] = {...};
 *
 * int main(void)
 * {
 *     credman_credential_t psk_credential = {
 *         .type = CREDMAN_TYPE_PSK,
 *         .tag = DTLS_SERVER_TAG,
 *         .params = {
 *             .psk = {
 *                 .key = { .s = psk_key, .len = sizeof(psk_key), },
 *             },
 *         },
 *     };
 *     res = credman_add(&psk_credential);
 *     if (res < 0) {
 *         printf("Error cannot add credential to system: %d\n", (int)res);
 *     }

 *     credman_credential_t credential = {
 *         .type = CREDMAN_TYPE_ECDSA,
 *         .tag = DTLS_SOCK_SERVER_TAG,
 *         .params = {
 *             .ecdsa = {
 *                 .private_key = server_ecdsa_priv_key,
 *                 .public_key = {
 *                     .x = server_ecdsa_pub_key_x,
 *                     .y = server_ecdsa_pub_key_y,
 *                 },
 *                 .client_keys = other_pubkeys,
 *                 .client_keys_size = sizeof(other_pubkeys) / sizeof(other_pubkeys[0]),
 *             },
 *         },
 *     };
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Above we see an example how to register a PSK and a ECC credential.
 *
 * First we need to include the header file for @ref net_sock_dtls "DTLS sock".
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * #include "net/credman.h"
 *
 * int main(void)
 * {
 *     credman_credential_t psk_credential = {
 *         .type = CREDMAN_TYPE_PSK,
 *         .tag = DTLS_SERVER_TAG,
 *         .params = {
 *             .psk = {
 *                 .key = { .s = psk_key, .len = sizeof(psk_key), },
 *             },
 *         },
 *     };
 *
 *     [...]
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * We tell @ref net_credman "credman" which credential to add by filling in
 * the credentials information in a struct @ref credman_credential_t. We are
 * using `CREDMAN_TYPE_PSK` for credman_credential_t::type for PSK credential.
 *
 * Next we must assign a @ref credman_tag_t "tag" for the credential. Tags
 * are unsigned integer value that are used to identify which DTLS sock have
 * access to which credential. Each DTLS sock will also be assigned a tag.
 * As a result, a sock can only use credentials that have the same tag as
 * theirs.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * res = credman_add(&psk_credential);
 * if (res < 0) {
 *     printf("Error cannot add credential to system: %d\n", (int)res);
 *     return;
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * After credential infos are filled, we can add it to the credential pool using
 * `credman_add`.
 *
 * Adding credentials of other types are all the same to the above except
 * credman_credential_t::type and credman_credential_t::param depends on type of
 * credential used.
 *
 * ### Server Operation
 *
 * After credentials are added, we can start the server.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * #include <stdio.h>
 *
 * #include "net/sock/dtls.h"
 *
 * static uint8_t buf[128];
 *
 * int main(void)
 * {
 *     // Add credentials
 *     [...]
 *
 *     // initialize server
 *     sock_udp_t udp_sock;
 *     sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
 *     local.port = 20220;
 *     sock_udp_create(&udp_sock, &local, NULL, 0);
 *
 *     sock_dtls_t dtls_sock;
 *
 *     if (sock_dtls_create(&dtls_sock, &udp_sock, DTLS_SOCK_SERVER_TAG, 0) < 0) {
 *         puts("Error creating DTLS sock");
 *         return 0;
 *     }
 *     sock_dtls_init_server(&dtls_sock);
 *
 *     while (1) {
 *         ssize_t res;
 *         sock_dtls_session_t session;
 *
 *         res = sock_dtls_recv(&dtls_sock, &session, buf, sizeof(buf)),
 *                              SOCK_NO_TIMEOUT);
 *         if (res > 0) {
 *             puts("Received a message");
 *             if (sock_dtls_send(&dtls_sock, buf, res, &session) < 0) {
 *                 puts("Error sending reply");
 *             }
 *         }
 *     }
 *     return 0;
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This is an example of a DTLS echo server.
 *
 * To create a DTLS sock, we first need to have an initialised UDP sock.
 * DTLS sock will inherit the port from given UDP sock. So the port to be used
 * later to listen to incoming DTLS packets will need to be set here already.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * sock_udp_t udp_sock;
 * sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
 * local.port = 20220;
 * sock_udp_create(&udp_sock, &local, NULL, 0);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Using the initialised UDP sock, we can then create our DTLS sock. We use
 * `DTLS_SOCK_SERVER_TAG`, which is defined as `10` beforehand, as our tag.
 * The last parameter of `sock_dtls_create` is the DTLS version to be used.
 *
 * Note that some DTLS implementation does not support earlier versions of DTLS.
 * We can see which version are supported by which DTLS implementation at this
 * @ref net_dtls "page".
 *
 * In case of error we stop the program.
 *
 * Then we call `sock_dtls_init_server` to initialize the server.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * #define DTLS_SOCK_SERVER_TAG (10)
 *
 * [...]
 *
 * sock_dtls_t dtls_sock;
 * sock_dtls_session_t session;
 *
 * if (sock_dtls_create(&dtls_sock, &udp_sock, DTLS_SOCK_SERVER_TAG, DTLSv12) < 0) {
 *     puts("Error creating DTLS sock");
 *     return 0;
 * }
 * sock_dtls_init_server(&dtls_sock);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Now we can listen to incoming packets using @ref sock_dtls_recv(). The application
 * waits indefinitely for new packets. If we want to timeout this wait period
 * we could alternatively set the `timeout` parameter of the function to a value
 * != @ref SOCK_NO_TIMEOUT. If an error occur we just ignore it and continue looping.
 * We can reply to an incoming message using its `session`.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * while (1) {
 *     ssize_t res;
 *     sock_dtls_session_t session;
 *
 *     res = sock_dtls_recv(&dtls_sock, &session, buf, sizeof(buf)),
 *                          SOCK_NO_TIMEOUT);
 *     if (res > 0) {
 *         puts("Received a message");
*         if (sock_dtls_send(&dtls_sock, buf, res, &session) < 0) {
 *             puts("Error sending reply");
 *         }
 *     }
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ### Client Operation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * #include "net/sock/udp.h"
 * #include "net/sock/dtls.h"
 * #include "net/ipv6/addr.h"
 * #include "net/credman.h"
 *
 * int main(void)
 * {
 *     sock_udp_t udp_sock;
 *     sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
 *     local.port = 12345;
 *     sock_udp_create(&udp_sock, &local_ep, NULL, 0);
 *
 *     sock_udp_ep_t remote;
 *     remote.port = DTLS_DEFAULT_PORT;
 *     remote.netif = gnrc_netif_iter(NULL)->pid;   // only if gnrc_netif_numoff == 1
 *
 *     if (!ipv6_addr_from_str((ipv6_addr_t *)remote.addr.ipv6, addr_str)) {
 *         puts("Error parsing destination address");
 *         return;
 *     }
 *
 *     res = sock_dtls_create(&dtls_sock, &udp_sock, DTLS_SOCK_CLIENT_TAG, 0);
 *     if (res < 0) {
 *         puts("Error creating DTLS sock");
 *         return;
 *     }
 *
 *     res = sock_dtls_establish_session(&dtls_sock, &remote, &session);
 *     if (res < 0) {
 *         printf("Error establishing session: %d\n", (int)res);
 *         goto end;
 *     }
 *
 *     res = sock_dtls_send(&dtls_sock, &session, data, datalen);
 *     if (res < 0) {
 *         printf("Error sending DTLS message: %d\n", (int)res);
 *         goto end;
 *     }
 *
 *     res = sock_dtls_recv(&dtls_sock, &session, rcv, sizeof(rcv), SOCK_NO_TIMEOUT);
 *     if (res < 0) {
 *         printf("Error receiving DTLS message: %d\n", (int)res);
 *         goto end;
 *     }
 *
 *     end:
 *         puts("Terminating");
 *         sock_dtls_close_session(&dtls_sock, &session);
 *         sock_dtls_destroy(&dtls_sock);
 *  }
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * This is an example of a DTLS echo client.
 *
 * Like the server, we must first create the UDP sock.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * sock_udp_t udp_sock;
 * sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
 * local.port = 12345;
 * sock_udp_create(&udp_sock, &local_ep, NULL, 0);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * After that, we set the address of the remote endpoint where the packet is
 * supposed to be sent to and the port the server is listening to which is
 * DTLS_DEFAULT_PORT(20220).
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * sock_udp_ep_t remote;
 * remote.port = DTLS_DEFAULT_PORT;
 * remote.netif = gnrc_netif_iter(NULL)->pid;   // only if gnrc_netif_numoff == 1
 * if (!ipv6_addr_from_str((ipv6_addr_t *)remote.addr.ipv6, addr_str)) {
 *     puts("Error parsing destination address");
 *     return;
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * After the UDP sock is created, we can proceed with creating the DTLS sock.
 * Before sending the packet, we must first establish a session with the remote
 * endpoint using @ref sock_dtls_establish_session(). If the handshake is
 * successfull and the session is established, we can use its `remote` to send
 * packets to it with @ref sock_dtls_send().
 *
 * If error during any of these operation, the session is closed and the sock
 * is destroyed using @ref sock_dtls_close_session() and
 * @ref sock_dtls_destroy().
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * res = sock_dtls_create(&dtls_sock, &udp_sock, DTLS_SOCK_CLIENT_TAG, 0);
 * if (res < 0) {
 *     puts("Error creating DTLS sock");
 *     return;
 * }
 *
 * res = sock_dtls_establish_session(&dtls_sock, &remote, &session);
 * if (res < 0) {
 *     printf("Error establishing session: %d\n", (int)res);
 *     goto end;
 * }
 *
 * res = sock_dtls_send(&dtls_sock, &session, data, datalen);
 * if (res < 0) {
 *     printf("Error sending DTLS message: %d\n", (int)res);
 *     goto end;
 * }
 *
 * res = sock_dtls_recv(&dtls_sock, &session, rcv, sizeof(rcv), SOCK_NO_TIMEOUT);
 * if (res < 0) {
 *     printf("Error receiving DTLS message: %d\n", (int)res);
 *     goto end;
 * }
 *
 * end:
 * puts("Terminating");
 * sock_dtls_close_session(&dtls_sock, &session);
 * sock_dtls_destroy(&dtls_sock);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * @{
 *
 * @file
 * @brief   DTLS sock definitions
 *
 * @author  Aiman Ismail <muhammadaimanbin.ismail@haw-hamburg.de>
 */

#ifndef NET_SOCK_DTLS_H
#define NET_SOCK_DTLS_H

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include "net/sock.h"
#include "net/sock/udp.h"
#include "net/credman.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Type for a DTLS sock object
 *
 * @note    API implementors: `struct sock_dtls` needs to be defined by
 *          implementation-specific `sock_dtls_types.h`.
 */
typedef struct sock_dtls sock_dtls_t;

/**
 * @brief Information about the established session with
 *        the remote endpoint. Used when sending and
 *        receiving data to the endpoint
 */
typedef struct sock_dtls_session sock_dtls_session_t;

/**
 * @brief Must be called once before any other use
 */
void sock_dtls_init(void);

/**
 * @brief Creates a new DTLS sock object
 *
 * @param[out] sock     The resulting DTLS sock object
 * @param[in] udp_sock  Existing UDP sock to be used
 * @param[in] tag       Credential tag of the sock. Used to get the right
 *                      credential from pool.
 * @param[in] method    Defines the method for the client or server to use.
 *
 * @return  0 on success.
 * @return  value < 0 on error
 */
int sock_dtls_create(sock_dtls_t *sock, sock_udp_t *udp_sock,
                     credman_tag_t tag,unsigned method);

/**
 * @brief Initialises the server to listen for incoming connections
 *
 * @param[in] sock      DTLS sock to listen to
 */
void sock_dtls_init_server(sock_dtls_t *sock);

/**
 * @brief Establish DTLS session to a server. Execute the handshake step in
 *        DTLS.
 *
 * @param[in]  sock     DLTS sock to use
 * @param[in]  ep       Endpoint to establish session with
 * @param[out] remote   The established session, cannot be NULL
 *
 * @return  0 on success
 * @return  value < 0 on other errors
 * @return  -EAGAIN, if @p timeout is `0` and no data is available.
 * @return  -EADDRNOTAVAIL, if the local endpoint of @p sock is not set.
 * @return  -EINVAL, if @p remote is invalid or @p sock is not properly
 *          initialized (or closed while sock_udp_recv() blocks).
 * @return  -ENOBUFS, if buffer space is not large enough to store received
 *          credentials.
 * @return  -ETIMEDOUT, if timed out when trying to establish session.
 */
int sock_dtls_establish_session(sock_dtls_t *sock, sock_udp_ep_t *ep,
                                sock_dtls_session_t *remote);

/**
 * @brief Close an existing DTLS session
 *
 * @pre `(sock != NULL) && (ep != NULL)`
 *
 * @param[in] sock      @ref sock_dtls_t, which the session is established on
 * @param[in] remote    Remote session to close
 */
void sock_dtls_close_session(sock_dtls_t *sock, sock_dtls_session_t *remote);

/**
 * @brief Decrypts and reads a message from a remote peer.
 *
 * @param[in] sock      DTLS sock to use.
 * @param[out] remote   Remote DTLS session of the received data.
 *                      Cannot be NULL.
 * @param[out] data     Buffer where the data should be stored.
 * @param[in] maxlen    Maximum memory available at @p data.
 * @param[in] timeout   Timeout for receive in microseconds.
 *                      If 0 and no data is available, the function returns
 *                      immediately.
 *                      May be SOCK_NO_TIMEOUT to wait until data
 *                      is available.
 *
 * @note Function may block if data not available and @p timeout != 0
 *
 * @return The number of bytes received on success
 * @return value < 0 on error
 * @return  -EADDRNOTAVAIL, if the local endpoint of @p sock is not set.
 * @return  -EAGAIN, if @p timeout is `0` and no data is available.
 * @return  -EINVAL, if @p remote is invalid or @p sock is not properly
 *          initialized (or closed while sock_udp_recv() blocks).
 * @return  -ENOBUFS, if buffer space is not large enough to store received
 *          data.
 * @return  -ENOMEM, if no memory was available to receive @p data.
 * @return  -ETIMEDOUT, if @p timeout expired.
 */
ssize_t sock_dtls_recv(sock_dtls_t *sock, sock_dtls_session_t *remote,
                       void *data, size_t maxlen, uint32_t timeout);

/**
 * @brief Encrypts and sends a message to a remote peer
 *
 * @param[in] sock      DTLS sock to use
 * @param[in] remote    DTLS session to use. A new session will be established
 *                      if no session exist between client and server.
 * @param[in] data      Pointer where the data to be send are stored
 * @param[in] len       Length of @p data to be send
 *
 * @note Function may block
 *
 * @return The number of bytes sent on success
 * @return value < 0 on error
 * @return  -EADDRINUSE, if sock_dtls_t::udp_sock has no local end-point.
 * @return  -EAFNOSUPPORT, if `remote->ep != NULL` and
 *          sock_dtls_session_t::ep::family of @p remote is != AF_UNSPEC and
 *          not supported.
 * @return  -EHOSTUNREACH, if sock_dtls_session_t::ep of @p remote is not
 *          reachable.
 * @return  -EINVAL, if sock_udp_ep_t::addr of @p remote->ep is an
 *          invalid address.
 * @return  -EINVAL, if sock_udp_ep_t::port of @p remote->ep is 0.
 * @return  -ENOMEM, if no memory was available to send @p data.
 */
ssize_t sock_dtls_send(sock_dtls_t *sock, sock_dtls_session_t *remote,
                       const void *data, size_t len);

/**
 * @brief Destroys DTLS sock created by `sock_dtls_create`
 *
 * @pre `(sock != NULL)`
 *
 * @param sock          DTLS sock to destroy
 */
void sock_dtls_destroy(sock_dtls_t *sock);

#include "sock_dtls_types.h"

#ifdef __cplusplus
}
#endif

#endif /* NET_SOCK_DTLS_H */
/** @} */
