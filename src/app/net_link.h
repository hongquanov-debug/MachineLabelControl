/**
 * @file net_link.h
 * @brief Modbus TCP server on W5500 (port 502, up to 4 connections), Spec §7.3.
 */
#ifndef NET_LINK_H
#define NET_LINK_H

void net_link_task(void *arg);

#endif
