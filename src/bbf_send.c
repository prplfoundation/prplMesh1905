/*
 *  Broadband Forum IEEE 1905.1/1a stack
 *
 *  Copyright (c) 2017, Broadband Forum
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  Subject to the terms and conditions of this license, each copyright
 *  holder and contributor hereby grants to those receiving rights under
 *  this license a perpetual, worldwide, non-exclusive, no-charge,
 *  royalty-free, irrevocable (except for failure to satisfy the
 *  conditions of this license) patent license to make, have made, use,
 *  offer to sell, sell, import, and otherwise transfer this software,
 *  where such license applies only to those patent claims, already
 *  acquired or hereafter acquired, licensable by such copyright holder or
 *  contributor that are necessarily infringed by:
 *
 *  (a) their Contribution(s) (the licensed copyrights of copyright holders
 *      and non-copyrightable additions of contributors, in source or binary
 *      form) alone; or
 *
 *  (b) combination of their Contribution(s) with the work of authorship to
 *      which such Contribution(s) was added by such copyright holder or
 *      contributor, if, at the time the Contribution is added, such addition
 *      causes such combination to be necessarily infringed. The patent
 *      license shall not apply to any other combinations which include the
 *      Contribution.
 *
 *  Except as expressly stated above, no rights or licenses from any
 *  copyright holder or contributor is granted under this license, whether
 *  expressly, by implication, estoppel or otherwise.
 *
 *  DISCLAIMER
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 *  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 */

#include "utils.h"
#include "al_send.h"
#include "1905_cmdus.h"
#include "1905_tlvs.h"
#include "bbf_tlvs.h"

#include "al_datamodel.h"
#include "al_extension.h"
#include "platform_interfaces.h"

#include <string.h> // memset(), memcmp(), ...
#include <stdio.h>    // snprintf

// Identify a processed BBF query
//
uint8_t bbf_query = 0;


////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

// Modify the provided pointers so that they now point to a list of pointers to
// "non1905NeighborDeviceListTLV" structures filled with all the pertaining
// information retrieved from the local device.
//
// Example: this is how you would use this function:
//
//   struct non1905NeighborDeviceListTLV **a;   uint8_t a_nr;
//
//   _obtainLocalNon1905NeighborsTLV(&a, &a_nr);
//
//   // a[0] -------> ptr to the first  "non1905NeighborDeviceListTLV" structure
//   // a[1] -------> ptr to the second "non1905NeighborDeviceListTLV" structure
//   // ...
//   // a[a_nr-1] --> ptr to the last   "non1905NeighborDeviceListTLV" structure
//
// 'non_1905_neighbors' is the non-1905 TLV array
//
// 'non_1905_neighbors_nr' is the number of entries in the above array
//
static void _obtainLocalNon1905NeighborsTLV(struct non1905NeighborDeviceListTLV ***non_1905_neighbors,
                                            uint8_t *non_1905_neighbors_nr)
{
    char                  **interfaces_names;
    uint8_t                   interfaces_names_nr;
    uint8_t                   i, j;

    if ((NULL == non_1905_neighbors) || (NULL == non_1905_neighbors_nr))
    {
        return;
    }

    *non_1905_neighbors    = NULL;
    *non_1905_neighbors_nr = 0;

    uint8_t (*al_mac_addresses)[6];
    uint8_t al_mac_addresses_nr;

    interfaces_names = PLATFORM_GET_LIST_OF_1905_INTERFACES(&interfaces_names_nr);

    for (i=0; i<interfaces_names_nr; i++)
    {
        struct interfaceInfo                 *x;

        struct non1905NeighborDeviceListTLV  *no;

        if (NULL == (x = PLATFORM_GET_1905_INTERFACE_INFO(interfaces_names[i])))
        {
            PLATFORM_PRINTF_DEBUG_WARNING("Could not retrieve neighbors of interface %s\n", interfaces_names[i]);
            continue;
        }

        al_mac_addresses = DMgetListOfInterfaceNeighbors(interfaces_names[i], &al_mac_addresses_nr);

        no  = (struct non1905NeighborDeviceListTLV *)memalloc(sizeof(struct non1905NeighborDeviceListTLV));

        no->tlv.type              = TLV_TYPE_NON_1905_NEIGHBOR_DEVICE_LIST;
        no->local_mac_address[0]  = x->mac_address[0];
        no->local_mac_address[1]  = x->mac_address[1];
        no->local_mac_address[2]  = x->mac_address[2];
        no->local_mac_address[3]  = x->mac_address[3];
        no->local_mac_address[4]  = x->mac_address[4];
        no->local_mac_address[5]  = x->mac_address[5];
        no->non_1905_neighbors_nr = 0;
        no->non_1905_neighbors    = NULL;

        // Decide if each neighbor is a 1905 or a non-1905 neighbor
        //
        if (x->neighbor_mac_addresses_nr != INTERFACE_NEIGHBORS_UNKNOWN)
        {
            uint8_t *al_mac_address_has_been_reported;

            // Keep track of all the AL MACs that the interface reports he is
            // seeing.
            //
            if (0 != al_mac_addresses_nr)
            {
                // Originally, none of the neighbors in the data model has been
                // reported...
                //
                al_mac_address_has_been_reported = (uint8_t *)memalloc(sizeof(uint8_t) * al_mac_addresses_nr);
                memset(al_mac_address_has_been_reported, 0x0, al_mac_addresses_nr);
            }

            for (j=0; j<x->neighbor_mac_addresses_nr; j++)
            {
                uint8_t *al_mac;
                uint8_t k;

                al_mac = DMmacToAlMac(x->neighbor_mac_addresses[j]);

                if (NULL == al_mac)
                {
                    // Non-1905 neighbor

                    uint8_t already_added;

                    // Make sure it has not already been added
                    //
                    already_added = 0;
                    for (k=0; k<no->non_1905_neighbors_nr; k++)
                    {
                        if (0 == memcmp(x->neighbor_mac_addresses[j], no->non_1905_neighbors[k].mac_address, 6))
                        {
                            already_added = 1;
                            break;
                        }
                    }

                    if (0 == already_added)
                    {
                        // This is a new neighbor
                        //
                        if (0 == no->non_1905_neighbors_nr)
                        {
                            no->non_1905_neighbors = (struct _non1905neighborEntries *)memalloc(sizeof(struct _non1905neighborEntries));
                        }
                        else
                        {
                            no->non_1905_neighbors = (struct _non1905neighborEntries *)memrealloc(no->non_1905_neighbors, sizeof(struct _non1905neighborEntries)*(no->non_1905_neighbors_nr+1));
                        }

                        no->non_1905_neighbors[no->non_1905_neighbors_nr].mac_address[0] = x->neighbor_mac_addresses[j][0];
                        no->non_1905_neighbors[no->non_1905_neighbors_nr].mac_address[1] = x->neighbor_mac_addresses[j][1];
                        no->non_1905_neighbors[no->non_1905_neighbors_nr].mac_address[2] = x->neighbor_mac_addresses[j][2];
                        no->non_1905_neighbors[no->non_1905_neighbors_nr].mac_address[3] = x->neighbor_mac_addresses[j][3];
                        no->non_1905_neighbors[no->non_1905_neighbors_nr].mac_address[4] = x->neighbor_mac_addresses[j][4];
                        no->non_1905_neighbors[no->non_1905_neighbors_nr].mac_address[5] = x->neighbor_mac_addresses[j][5];

                        no->non_1905_neighbors_nr++;
                    }
                }
                else
                {
                    // 1905 neighbor

                    free(al_mac);
                }
            }
            free_1905_INTERFACE_INFO(x);

            if (al_mac_addresses_nr > 0 && NULL != al_mac_address_has_been_reported)
            {
                free(al_mac_address_has_been_reported);
            }
        }
        free(al_mac_addresses);

        // At this point we have, for this particular interface, all the non
        // 1905 neighbors in "no" and all 1905 neighbors in "yes".
        //
        // We just need to add "no" and "yes" to the "non_1905_neighbors" and
        // "neighbors" lists and proceed to the next interface.
        //
        if (no->non_1905_neighbors_nr > 0)
        {
            // Add this to the list of non-1905 neighbor TLVs
            //
            if (0 == *non_1905_neighbors_nr)
            {
                *non_1905_neighbors = (struct non1905NeighborDeviceListTLV **)memalloc(sizeof(struct non1905NeighborDeviceListTLV *));
            }
            else
            {
                *non_1905_neighbors = (struct non1905NeighborDeviceListTLV **)memrealloc(*non_1905_neighbors, sizeof(struct non1905NeighborDeviceListTLV *)*(*non_1905_neighbors_nr+1));
            }

            (*non_1905_neighbors)[*non_1905_neighbors_nr] = no;
            (*non_1905_neighbors_nr)++;
        }
        else
        {
            free(no);
        }
    }

    free_LIST_OF_1905_INTERFACES(interfaces_names, interfaces_names_nr);
}

// Auxiliary function to reorganize data obtained from
// '_obtainLocalNon1905NeighborsTLV()' which classifies the non1905 neighbors
// entries by local interfaces (note: one neighbor could be present in more
// than one interface)
//
// This function processes this structure in order to return a MAC address list
// of all non1905 neighbors.
//
// This function is required by '_obtainLocalNon1905MetricsTLV()'
//
// 'non1905_neighbors' is a pointer to the input array of non-1905 TLVs
//
// 'non1905_neighbors_nr' is the number of entries in this array
//
// 'mac_addresses_nr' is the number of different mac addresses found in the
// non-1905 TLVs.
//
// Return a list of non-repeated mac addresses found in the input arguments
// (non1905_neighbors). So, it's the list of non-1905 neighbor mac addresses.
//
static uint8_t (*_getListOfNon1905Neighbors(struct non1905NeighborDeviceListTLV  **non1905_neighbors,
                                          uint8_t                                  non1905_neighbors_nr,
                                          uint8_t                                 *mac_addresses_nr))[6]
{
    uint8_t i, j, k;

    uint8_t total;
    uint8_t (*ret)[6];

    if ((NULL == non1905_neighbors) || (NULL == mac_addresses_nr))
    {
        return NULL;
    }

    total = 0;
    ret   = NULL;

    for (i=0; i<non1905_neighbors_nr; i++)
    {
        for (j=0; j<non1905_neighbors[i]->non_1905_neighbors_nr; j++)
        {
            // Check for duplicates
            //
            uint8_t already_present;

            already_present = 0;
            for (k=0; k<total; k++)
            {
                if (0 == memcmp(&ret[k], non1905_neighbors[i]->non_1905_neighbors[j].mac_address, 6))
                {
                    already_present = 1;
                    break;
                }
            }

            if (1 == already_present)
            {
                continue;
            }

            // If we get here, this is a new neighbor and we need to add it to
            // the list
            //
            if (NULL == ret)
            {
                ret = (uint8_t (*)[6])memalloc(sizeof(uint8_t[6]));
            }
            else
            {
                ret = (uint8_t (*)[6])memrealloc(ret, sizeof(uint8_t[6])*(total + 1));
            }
            memcpy(&ret[total], non1905_neighbors[i]->non_1905_neighbors[j].mac_address, 6);

            total++;
        }
    }

    *mac_addresses_nr = total;

    return ret;
}

// Auxiliary function to reorganize data obtained from
// '_obtainLocalNon1905NeighborsTLV()' which classifies the non1905 neighbors
// entries by local interfaces (note: one neighbor could be present in more
// than one interface)
//
// Given a non1905 neighbor, this function returns the list of links with this
// node This function is required by '_obtainLocalNon1905MetricsTLV()'
//
// 'non1905_neighbors' is the input array of non-1905 TLVs
//
// 'non1905_neighbors_nr' is the number of entries in the above TLV array
// (non-1905_neighbors)
//
// 'neighbor_mac_address' is the mac address of the requested non-1905 device
//
// 'interfaces' is a list of interfaces' names. It's part of the response and
// it's correlated with 'links_nr'
//
// 'links_nr' is the number of links found with the non-1905 device with mac
// 'neighbor_mac_address'
//
// Return a list of links with the non-1905 device wit 'neighbor_mac_address'
// mac address
//
uint8_t (*_getListOfLinksWithNon1905Neighbor(struct non1905NeighborDeviceListTLV  **non1905_neighbors, uint8_t non1905_neighbors_nr,
                                           uint8_t *neighbor_mac_address, char ***interfaces, uint8_t *links_nr))[6]
{
    uint8_t i, j;
    uint8_t total;

    uint8_t (*ret)[6];
    char  **intfs;

    if ((NULL == non1905_neighbors) || (NULL == neighbor_mac_address) || (NULL == interfaces) || (NULL == links_nr))
    {
        return NULL;
    }

    total = 0;
    ret   = NULL;
    intfs = NULL;

    for (i=0; i<non1905_neighbors_nr; i++)
    {
        if (NULL != non1905_neighbors[i])
        {
            for (j=0; j<non1905_neighbors[i]->non_1905_neighbors_nr; j++)
            {
                // Filter neighbor (we are just interested in
                // 'neighbor_mac_address')
                //
                if (0 != memcmp(neighbor_mac_address, non1905_neighbors[i]->non_1905_neighbors[j].mac_address, 6))
                {
                    continue;
                }

                if (NULL == ret)
                {
                    ret   = (uint8_t (*)[6])memalloc(sizeof(uint8_t[6]));
                    intfs = (char **)memalloc(sizeof(char *));
                }
                else
                {
                    ret   = (uint8_t (*)[6])memrealloc(ret, sizeof(uint8_t[6])*(total + 1));
                    intfs = (char **)memrealloc(intfs, sizeof(char *)*(total + 1));
                }
                memcpy(&ret[total], non1905_neighbors[i]->non_1905_neighbors[j].mac_address, 6);
                intfs[total] = DMmacToInterfaceName(non1905_neighbors[i]->local_mac_address);

                total++;
            }
        }
    }

    *links_nr   = total;
    *interfaces = intfs;

    return ret;
}

// Free the contents of the pointers filled by a previous call to
// "_obtainLocalNon1905NeighborsTLV()".
//
// This function is called with the same arguments as
// "_obtainLocalNon1905NeighborsTLV()"
//
// 'non_1905_neighbors' is an array of non-1590 TLVs
//
// 'non_1905_neighbors_nr' is the size og the above array
//
static void _freeLocalNon1905NeighborsTLV(struct non1905NeighborDeviceListTLV ***non_1905_neighbors, uint8_t *non_1905_neighbors_nr)
{
    uint8_t i;

    if ((NULL != non_1905_neighbors) && (NULL != *non_1905_neighbors) && (NULL != non_1905_neighbors_nr))
    {
        for (i=0; i<(*non_1905_neighbors_nr); i++)
        {
            if (NULL != (*non_1905_neighbors)[i])
            {
                if ((*non_1905_neighbors)[i]->non_1905_neighbors_nr > 0)
                {
                    free((*non_1905_neighbors)[i]->non_1905_neighbors);
                }
                free((*non_1905_neighbors)[i]);
            }
        }
        free(*non_1905_neighbors);
    }
}

// Return a list of Tx metrics TLVs and/or a list of Rx metrics TLVs involving
// the local node and the non1905 neighbor whose MAC address matches
// 'specific_neighbor'.
//
// 'destination' can be either 'LINK_METRIC_QUERY_TLV_ALL_NEIGHBORS' (in which
// case argument 'specific_neighbor' is ignored) or
// 'LINK_METRIC_QUERY_TLV_SPECIFIC_NEIGHBOR' (in which case 'specific_neighbor'
// is the MAC of the non1905 node at the other end of the link whose metrics
// are being reported.
//
// 'specific_neighbor' is the mac address of the non-1905 neighbor
//
// 'metrics_type' can be 'LINK_METRIC_QUERY_TLV_TX_LINK_METRICS_ONLY',
// 'LINK_METRIC_QUERY_TLV_RX_LINK_METRICS_ONLY' or
// 'LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS'.
//
// 'tx' is an output argument which will be set to NULL if 'metrics_type' is
// set to 'LINK_METRIC_QUERY_TLV_RX_LINK_METRICS_ONLY' or point to a list of 1
// or more (depending on the value of 'destination') pointers to Tx metrics
// TLVs.
//
// 'rx' is an output argument which will be set to NULL if 'metrics_type' is
// set to 'LINK_METRIC_QUERY_TLV_TX_LINK_METRICS_ONLY' or point to a list of 1
// or more (depending on the value of 'destination') pointers to Rx metrics
// TLVs.
//
// 'nr' is an output argument set to the number of elements if rx and/or tx.
//
// If there is a problem (example: a specific neighbor was not found), this
// function returns '0', otherwise it returns '1'.
//
static void _obtainLocalNon1905MetricsTLV(uint8_t destination, uint8_t *specific_neighbor, uint8_t metrics_type,
                                          struct transmitterLinkMetricTLV ***tx,
                                          struct receiverLinkMetricTLV    ***rx,
                                          uint8_t *nr)
{
    uint8_t (*mac_addresses)[6];
    uint8_t   mac_addresses_nr;
    uint8_t   empty_addresses[6] = {0x00,0x00,0x00,0x00,0x00,0x00};

    struct transmitterLinkMetricTLV   **tx_tlvs;
    struct receiverLinkMetricTLV      **rx_tlvs;

    uint8_t total_tlvs;
    uint8_t i, j;

    struct non1905NeighborDeviceListTLV  **non1905_neighbors;
    uint8_t                                  non1905_neighbors_nr;


    // Get the list of non1905 neighbors (classified by interface)
    //
    _obtainLocalNon1905NeighborsTLV(&non1905_neighbors, &non1905_neighbors_nr);

    mac_addresses_nr = 0;
    mac_addresses = _getListOfNon1905Neighbors(non1905_neighbors, non1905_neighbors_nr, &mac_addresses_nr);

    // We will need either 1 or 'al_mac_addresses_nr' Rx and/or Tx TLVs,
    // depending on the value of the 'destination' argument (ie. one Rx and/or
    // Tx TLV for each neighbor whose metrics we are going to report)
    //
    rx_tlvs = NULL;
    tx_tlvs = NULL;
    if (mac_addresses_nr > 0)
    {
        if (LINK_METRIC_QUERY_TLV_ALL_NEIGHBORS == destination)
        {
            if (
                 LINK_METRIC_QUERY_TLV_TX_LINK_METRICS_ONLY        == metrics_type ||
                 LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS == metrics_type
               )
            {
                tx_tlvs = (struct transmitterLinkMetricTLV **)memalloc(sizeof(struct transmitterLinkMetricTLV*) * mac_addresses_nr);
            }
            if (
                 LINK_METRIC_QUERY_TLV_RX_LINK_METRICS_ONLY        == metrics_type ||
                 LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS == metrics_type
               )
            {
                rx_tlvs = (struct receiverLinkMetricTLV **)memalloc(sizeof(struct receiverLinkMetricTLV*) * mac_addresses_nr);
            }
        }
        else
        {
            if (
                 LINK_METRIC_QUERY_TLV_TX_LINK_METRICS_ONLY        == metrics_type ||
                 LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS == metrics_type
               )
            {
                tx_tlvs = (struct transmitterLinkMetricTLV **)memalloc(sizeof(struct transmitterLinkMetricTLV*) * 1);
            }
            if (
                 LINK_METRIC_QUERY_TLV_RX_LINK_METRICS_ONLY        == metrics_type ||
                 LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS == metrics_type
               )
            {
                rx_tlvs = (struct receiverLinkMetricTLV **)memalloc(sizeof(struct receiverLinkMetricTLV*) * 1);
            }
        }
    }

    // Next, for each neighbor, fill the corresponding TLV structure (Rx, Tx or
    // both) that contains the information regarding all possible links that
    // "join" our local node with that neighbor.
    //
    total_tlvs = 0;
    for (i=0; i<mac_addresses_nr; i++)
    {
        uint8_t  (*remote_macs)[6];
        char   **local_interfaces;
        uint8_t    links_nr = 0;

        // Check if we are really interested in obtaining metrics information
        // regarding this particular neighbor
        //
        if (
             LINK_METRIC_QUERY_TLV_SPECIFIC_NEIGHBOR == destination          &&
             0 != memcmp(mac_addresses[i], specific_neighbor, 6)
           )
        {
            // Not interested
            //
            continue;
        }

        // Obtain the list of "links" that connect our AL node with this
        // specific neighbor.
        //
        remote_macs = _getListOfLinksWithNon1905Neighbor(non1905_neighbors, non1905_neighbors_nr,
                                                         mac_addresses[i], &local_interfaces, &links_nr);

        if (links_nr > 0)
        {

            // If there are 1 or more links between the local node and the
            // neighbor, first fill the TLV "header"
            //
            if (NULL != tx_tlvs)
            {
                tx_tlvs[total_tlvs] = (struct transmitterLinkMetricTLV *)memalloc(sizeof(struct transmitterLinkMetricTLV));

                                tx_tlvs[total_tlvs]->tlv.type                    = BBF_TLV_TYPE_NON_1905_TRANSMITTER_LINK_METRIC;
                memcpy(tx_tlvs[total_tlvs]->local_al_address,             DMalMacGet(),                       6);
                memcpy(tx_tlvs[total_tlvs]->neighbor_al_address,          empty_addresses,                    6);
                                tx_tlvs[total_tlvs]->transmitter_link_metrics_nr = links_nr;
                                tx_tlvs[total_tlvs]->transmitter_link_metrics    = memalloc(sizeof(struct _transmitterLinkMetricEntries) * links_nr);
            }
            if (NULL != rx_tlvs)
            {
                rx_tlvs[total_tlvs] = (struct receiverLinkMetricTLV *)memalloc(sizeof(struct receiverLinkMetricTLV));

                                rx_tlvs[total_tlvs]->tlv.type                    = BBF_TLV_TYPE_NON_1905_RECEIVER_LINK_METRIC;
                memcpy(rx_tlvs[total_tlvs]->local_al_address,             DMalMacGet(),                       6);
                memcpy(rx_tlvs[total_tlvs]->neighbor_al_address,          empty_addresses,                    6);
                                rx_tlvs[total_tlvs]->receiver_link_metrics_nr    = links_nr;
                                rx_tlvs[total_tlvs]->receiver_link_metrics       = memalloc(sizeof(struct _receiverLinkMetricEntries) * links_nr);
            }

            // ...and then, for each link, fill the specific link information:
            //
            for (j=0; j<links_nr; j++)
            {
                struct interfaceInfo *f;
                struct linkMetrics   *l;

                f = PLATFORM_GET_1905_INTERFACE_INFO(local_interfaces[j]);
                l = PLATFORM_GET_LINK_METRICS(local_interfaces[j], remote_macs[j]);

                if (NULL != tx_tlvs)
                {
                    memcpy(tx_tlvs[total_tlvs]->transmitter_link_metrics[j].local_interface_address,    DMinterfaceNameToMac(local_interfaces[j]), 6);
                    memcpy(tx_tlvs[total_tlvs]->transmitter_link_metrics[j].neighbor_interface_address, remote_macs[j],                            6);

                    if (NULL == f)
                    {
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].intf_type = MEDIA_TYPE_UNKNOWN;
                    }
                    else
                    {
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].intf_type = f->interface_type;
                    }
                    tx_tlvs[total_tlvs]->transmitter_link_metrics[j].bridge_flag = 0;

                    if (NULL == l)
                    {
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].packet_errors           =  0;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].transmitted_packets     =  0;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].mac_throughput_capacity =  0;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].link_availability       =  0;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].phy_rate                =  0;
                    }
                    else
                    {
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].packet_errors           =  l->tx_packet_errors;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].transmitted_packets     =  l->tx_packet_ok;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].mac_throughput_capacity =  l->tx_max_xput;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].link_availability       =  l->tx_link_availability;
                        tx_tlvs[total_tlvs]->transmitter_link_metrics[j].phy_rate                =  l->tx_phy_rate;
                    }
                }

                if (NULL != rx_tlvs)
                {
                    memcpy(rx_tlvs[total_tlvs]->receiver_link_metrics[j].local_interface_address,    DMinterfaceNameToMac(local_interfaces[j]), 6);
                    memcpy(rx_tlvs[total_tlvs]->receiver_link_metrics[j].neighbor_interface_address, remote_macs[j],                            6);

                    if (NULL == f)
                    {
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].intf_type = MEDIA_TYPE_UNKNOWN;
                    }
                    else
                    {
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].intf_type = f->interface_type;
                    }

                    if (NULL == l)
                    {
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].packet_errors           =  0;
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].packets_received        =  0;
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].rssi                    =  0;
                    }
                    else
                    {
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].packet_errors           =  l->rx_packet_errors;
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].packets_received        =  l->rx_packet_ok;
                        rx_tlvs[total_tlvs]->receiver_link_metrics[j].rssi                    =  l->rx_rssi;
                    }
                }

                if (NULL != f)
                {
                    free_1905_INTERFACE_INFO(f);
                }
                if (NULL != l)
                {
                    free_LINK_METRICS(l);
                }
            }

            total_tlvs++;
        }

        DMfreeListOfLinksWithNeighbor(remote_macs, local_interfaces, links_nr);
    }

    free(mac_addresses);

    _freeLocalNon1905NeighborsTLV(&non1905_neighbors, &non1905_neighbors_nr);

    if (
         LINK_METRIC_QUERY_TLV_SPECIFIC_NEIGHBOR == destination &&
         total_tlvs == 0
       )
    {
        // Specific neighbor not found
        //
        *nr = 0;
        *tx = NULL;
        *rx = NULL;
    }
    else
    {
        *nr = total_tlvs;
        *tx = tx_tlvs;
        *rx = rx_tlvs;
    }
}

// Free the contents of the pointers filled by a previous call to
// "_obtainLocalNon1905MetricsTLV()".
//
// This function is called with the same three last arguments as
// "_obtainLocalNon1905MetricsTLV()"
//
// 'tx_tlvs' is an array of transmit metrics TLV
//
// 'rx_tlvs' is an array of receive metrics TLV
//
// 'total_tlvs' is the number of metric pairs (tx/rx) TLVs
//
static void _freeLocalNon1905MetricsTLVs(struct transmitterLinkMetricTLV ***tx_tlvs, struct receiverLinkMetricTLV ***rx_tlvs, uint8_t *total_tlvs)
{
    uint8_t i;

    if (NULL != tx_tlvs && NULL != *tx_tlvs)
    {
        for (i=0; i<*total_tlvs; i++)
        {
            if ((*tx_tlvs)[i]->transmitter_link_metrics_nr > 0 && NULL != (*tx_tlvs)[i]->transmitter_link_metrics)
            {
                free((*tx_tlvs)[i]->transmitter_link_metrics);
            }
            free((*tx_tlvs)[i]);
        }
        free(*tx_tlvs);
    }
    if (NULL != rx_tlvs && NULL != *rx_tlvs)
    {
        for (i=0; i<*total_tlvs; i++)
        {
            if ((*rx_tlvs)[i]->receiver_link_metrics_nr > 0 && NULL != (*rx_tlvs)[i]->receiver_link_metrics)
            {
                free((*rx_tlvs)[i]->receiver_link_metrics);
            }
            free((*rx_tlvs)[i]);
        }
        free(*rx_tlvs);
    }
}


////////////////////////////////////////////////////////////////////////////////
// Datamodel extension callbacks
////////////////////////////////////////////////////////////////////////////////

void CBKObtainBBFExtendedLocalInfo(struct vendorSpecificTLV ***extensions,
                                   uint8_t                      *nr)
{
    struct transmitterLinkMetricTLV  **tx_tlvs;
    struct receiverLinkMetricTLV     **rx_tlvs;
    struct vendorSpecificTLV          *vendor_specific;
    struct vendorSpecificTLV         **tlvs;

    uint8_t total_tlvs;
    uint8_t total_extensions;

    // Currently, the BBF actor only takes care of TLVs containing non-1905
    // metrics. This may be extended in the future.
    //

    //
    // Obtain BBF extensions:
    // - non-1905 metrics
    //
    _obtainLocalNon1905MetricsTLV(LINK_METRIC_QUERY_TLV_ALL_NEIGHBORS, NULL,
                                  LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS,
                                  &tx_tlvs, &rx_tlvs, &total_tlvs);


    tlvs = NULL;
    total_extensions = 0;
    if (total_tlvs > 0)
    {
        uint8_t i;

        // Build two TLV extensions (tx and rx) per neighbor
        //
        tlvs = (struct vendorSpecificTLV **)memalloc(sizeof(struct vendorSpecificTLV*) * total_tlvs * 2);

        for (i=0; i<total_tlvs; i++)
        {
            vendor_specific = vendorSpecificTLVEmbedExtension(&tx_tlvs[i]->tlv, forge_bbf_TLV_from_structure, (uint8_t *)BBF_OUI);
            tlvs[total_extensions++] = vendor_specific;
        }

        for (i=0; i<total_tlvs; i++)
        {
            vendor_specific = vendorSpecificTLVEmbedExtension(&rx_tlvs[i]->tlv, forge_bbf_TLV_from_structure, (uint8_t *)BBF_OUI);
            tlvs[total_extensions++] = vendor_specific;
        }
    }
    else
    {
        struct linkMetricResultCodeTLV    *result_tlvs;

        // A 'result code' TLV will indicate that there are not available
        // metrics. This (mark) will later force the update of metrics
        // extensions
        //
        result_tlvs = (struct linkMetricResultCodeTLV *)memalloc(sizeof(struct linkMetricResultCodeTLV));
        result_tlvs->tlv.type  = BBF_TLV_TYPE_NON_1905_LINK_METRIC_RESULT_CODE;
        result_tlvs->result_code = LINK_METRIC_RESULT_CODE_TLV_INVALID_NEIGHBOR;

        vendor_specific = vendorSpecificTLVEmbedExtension(&result_tlvs->tlv, forge_bbf_TLV_from_structure, (uint8_t *)BBF_OUI);

        tlvs = (struct vendorSpecificTLV **)memalloc(sizeof(struct vendorSpecificTLV*));
        tlvs[total_extensions++] = vendor_specific;

        // Free no longer used resources
        //
        free_bbf_TLV_structure(&result_tlvs->tlv);
    }

    // Free tx_tlvs and rx_tlvs (no longer used)
    //
    _freeLocalNon1905MetricsTLVs(&tx_tlvs, &rx_tlvs, &total_tlvs);

    *extensions = tlvs;
    *nr         = total_extensions;
}


void CBKUpdateBBFExtendedInfo(struct vendorSpecificTLV **extensions, uint8_t nr, uint8_t *al_mac_address)
{
    uint8_t                         i;
    uint8_t                         non1905_metrics = 0;
    uint8_t                        *dm_extensions_nr;
    struct vendorSpecificTLV   ***dm_extensions;       // device extensions

    if ((NULL == extensions) || (NULL == al_mac_address))
    {
        return;
    }

    // Point to the datamodel extensions of the device with 'al_mac_address'
    // address
    //
    dm_extensions = DMextensionsGet(al_mac_address, &dm_extensions_nr);

    if (dm_extensions == NULL) {
        return;
    }

    // Update the extended local info
    //
    // The 1905 core stack simply provides a pointer to this 'extended info'
    // (one pointer for each 1905 device), which is modeled as an array of
    // Vendor Specific TLVs (common for any third-party implementation)
    //
    // The core stack has no idea about the content of this extended info, so
    // it's the third-party implementations responsibility to manage this area.
    //
    // The only place where the core stack manages this pointer is when a
    // device is removed from the datamodel. In this case, the allocated memory
    // for both the Vendor Specific TLVs and the array of pointers to these
    // TLVs is freed by the core stack itself (no need to ask for each actor to
    // free its own TLVs)
    //
    // How to update the extended data depends on the implementer.
    // Currenty, BBF only supports one type of extended info: non-1905 metrics,
    // and the way they are updated is:
    // - remove the existing BBF non-1905 metrics TLVs
    // - add the new BBF non-1905 metrics TLVs
    //
    // It's a complete replacement, because the whole updated data comes
    // embedded in a single CMDU.
    //
    // Future extensions may require other policy.
    //

    // Review incoming extensions.
    // Right now, BBF only supports non-1905 metrics extensions, so this check
    // would be unnecesary.
    //
    for (i=0; i<nr; i++)
    {
        if ((BBF_TLV_TYPE_NON_1905_TRANSMITTER_LINK_METRIC == extensions[i]->m[0]) ||
            (BBF_TLV_TYPE_NON_1905_RECEIVER_LINK_METRIC    == extensions[i]->m[0]) ||
            (BBF_TLV_TYPE_NON_1905_LINK_METRIC_RESULT_CODE == extensions[i]->m[0]) )
        {
            non1905_metrics = 1;
            break;
        }
    }

    // Update non-1905 metrics (replace older metrics)
    //
    if (non1905_metrics)
    {
        uint8_t original_nr;

        original_nr = *dm_extensions_nr;

        // Remove old entries
        //
        for (i=0; i<(*dm_extensions_nr); i++)
        {
            if ((BBF_TLV_TYPE_NON_1905_TRANSMITTER_LINK_METRIC == (*dm_extensions)[i]->m[0]) ||
                (BBF_TLV_TYPE_NON_1905_RECEIVER_LINK_METRIC    == (*dm_extensions)[i]->m[0]) ||
                (BBF_TLV_TYPE_NON_1905_LINK_METRIC_RESULT_CODE == (*dm_extensions)[i]->m[0]) )
            {
                free_1905_TLV_structure(&(*dm_extensions)[i]->tlv);

                if (i == (*dm_extensions_nr))
                {
                    // Last element. It will automatically be removed below
                    // (keep reading)
                }
                else
                {
                    (*dm_extensions)[i] = (*dm_extensions)[(*dm_extensions_nr)-1];
                    i--;
                }
                (*dm_extensions_nr)--;

            }
        }

        // Extensions array size may change
        //
        if (original_nr != ((*dm_extensions_nr) + nr))
        {
            if (0 == ((*dm_extensions_nr) + nr))
            {
                free((*dm_extensions));
            }
            else if (0 == original_nr)
            {
                (*dm_extensions) = (struct vendorSpecificTLV **)memalloc(sizeof(struct vendorSpecificTLV *) * ((*dm_extensions_nr) + nr));
            }
            else
            {
                (*dm_extensions) = (struct vendorSpecificTLV **)memrealloc((*dm_extensions), sizeof(struct vendorSpecificTLV *) * ((*dm_extensions_nr) + nr));
            }
        }

        // Add new entries
        //
        for (i=0; i<nr; i++)
        {
            (*dm_extensions)[*dm_extensions_nr] = extensions[i];
            (*dm_extensions_nr)++;
        }
    }
}


void CBKDumpBBFExtendedInfo(uint8_t **memory_structure,
                            uint8_t   structure_nr,
                            visitor_callback callback,
                            void  (*write_function)(const char *fmt, ...),
                            const char *prefix)
{
#define MAX_PREFIX 100
    char                        new_prefix[MAX_PREFIX];
    struct vendorSpecificTLV   *extension_tlv;
    struct vendorSpecificTLV  **tx_metrics;
    struct vendorSpecificTLV  **rx_metrics;
    uint8_t                     (*mac_metrics)[6];
    uint8_t                       metrics_nr;
    struct tlv                   *real_output;
    uint8_t                      *TO_interface_mac_address; // Neighbor interface
                                                          // mac adress
    uint8_t                       i;
    uint8_t                       j;
    uint8_t                       dump_ignore;

    if ((NULL == memory_structure) || (NULL == callback) || (NULL == write_function) || (NULL == prefix))
    {
        return;
    }

    // Datamodel dedicates a section to the non-standard TLVs. Actually, this
    // section is an array of Vendor Specific TLVs, each one of them is
    // embedding a non-standard TLV.
    //
    // This array may contain TLVs belonging to different registered extenders.
    // Each registered extender is responsible for taking its own TLVs and
    // present the data in an organized manner.
    //
    // Currently, BBF only defines non-1905 metrics as non-standard TLVs. The
    // way they are going to be presented is the same way the standard does
    // with the standard metric TVLs (tx/rx metrics organized by device)

    // So, the first step is to run through all the Vendor Specific TLVs,
    // analyzed them and reorganize data.
    //
    tx_metrics  = NULL;
    rx_metrics  = NULL;
    mac_metrics = NULL;
    real_output = NULL;
    metrics_nr  = 0;
    for (i=0; i<structure_nr; i++)
    {
        dump_ignore = 0;

        extension_tlv = (struct vendorSpecificTLV *)(memory_structure[i]);

        if ((NULL == extension_tlv) || (0 != memcmp(extension_tlv->vendorOUI, BBF_OUI, 3)))
        {
            // This not a BBF TLV. Ignored it.
            //
            dump_ignore = 1;
        }

        if (0 == dump_ignore)
        {
            real_output = parse_bbf_TLV_from_packet(extension_tlv->m);

            // Obtain the neighbor interface MAC belonging to this metric
            //
            // TODO: It's assumed one link per non-1905 neighbor. Is it
            // possible to know that a 1905 device is connected to a non-1905
            // device by more than one interface? By now, this scenario is
            // considering two different non-1905 nodes (one per each
            // interface)
            //
            if (BBF_TLV_TYPE_NON_1905_TRANSMITTER_LINK_METRIC == real_output->type)
            {
                struct transmitterLinkMetricTLV *p;

                p = (struct transmitterLinkMetricTLV *)real_output;

                TO_interface_mac_address = p->transmitter_link_metrics[0].neighbor_interface_address;
            }
            else if (BBF_TLV_TYPE_NON_1905_RECEIVER_LINK_METRIC == real_output->type)
            {
                struct receiverLinkMetricTLV *p;

                p = (struct receiverLinkMetricTLV *)real_output;

                TO_interface_mac_address = p->receiver_link_metrics[0].neighbor_interface_address;
            }
            else
            {
                // This is not a BBF TLV to dump. Ignore it.
                //
                dump_ignore = 1;
            }
        }

        if (0 == dump_ignore)
        {
            // Next, search for an existing entry with the same interface MAC
            // address
            //
            for (j=0; j<metrics_nr; j++)
            {
                if (0 == memcmp(mac_metrics[j], TO_interface_mac_address, 6))
                {
                    break;
                }
            }

            if (j == metrics_nr)
            {
                // A matching entry was *not* found. Create a new one
                //
                if (0 == metrics_nr)
                {
                    tx_metrics  = (struct vendorSpecificTLV **)memalloc(sizeof(struct vendorSpecificTLV *));
                    rx_metrics  = (struct vendorSpecificTLV **)memalloc(sizeof(struct vendorSpecificTLV *));
                    mac_metrics =                (uint8_t (*)[6])memalloc(sizeof(uint8_t[6]));
                }
                else
                {
                    tx_metrics  = (struct vendorSpecificTLV **)memrealloc(tx_metrics, sizeof(struct vendorSpecificTLV *) * (metrics_nr+1));
                    rx_metrics  = (struct vendorSpecificTLV **)memrealloc(rx_metrics, sizeof(struct vendorSpecificTLV *) * (metrics_nr+1));
                    mac_metrics =                (uint8_t (*)[6])memrealloc(mac_metrics, sizeof(uint8_t[6]) * (metrics_nr+1));
                }

                // Keep metrics owner track
                //
                memcpy(mac_metrics[j], TO_interface_mac_address, 6);
                tx_metrics[j] = NULL;
                rx_metrics[j] = NULL;

                metrics_nr++;
            }

            // Update the entry.
            //
            if (BBF_TLV_TYPE_NON_1905_TRANSMITTER_LINK_METRIC == real_output->type)
            {
                tx_metrics[j] = extension_tlv;
            }
            else
            {
                rx_metrics[j] = extension_tlv;
            }
        }

        // Free resources no longer used
        //
        free_bbf_TLV_structure(real_output);
    }

    // Now, present data in an organized way
    //
    snprintf(new_prefix, MAX_PREFIX-1, "%sOUI(0x%02x%02x%02x)->non1905_metrics_nr: %d", prefix, BBF_OUI[0], BBF_OUI[1], BBF_OUI[2], metrics_nr);
    new_prefix[MAX_PREFIX-1] = 0x0;
    write_function("%s\n", new_prefix);

    for (i=0; i<metrics_nr; i++)
    {
        snprintf(new_prefix, MAX_PREFIX-1, "%sOUI(0x%02x%02x%02x)->non1905_metrics[%d]->tx->", prefix, BBF_OUI[0], BBF_OUI[1], BBF_OUI[2], i);
        new_prefix[MAX_PREFIX-1] = 0x0;
        if (NULL != tx_metrics[i])
        {
            visit_bbf_TLV_structure(parse_bbf_TLV_from_packet(tx_metrics[i]->m), callback, write_function, new_prefix);
        }
        snprintf(new_prefix, MAX_PREFIX-1, "%sOUI(0x%02x%02x%02x)->non1905_metrics[%d]->rx->", prefix, BBF_OUI[0], BBF_OUI[1], BBF_OUI[2], i);
        new_prefix[MAX_PREFIX-1] = 0x0;
        if (NULL != rx_metrics[i])
        {
            visit_bbf_TLV_structure(parse_bbf_TLV_from_packet(rx_metrics[i]->m), callback, write_function, new_prefix);
        }
    }

    // Free resources
    //
    free(tx_metrics);
    free(rx_metrics);
    free(mac_metrics);
}


////////////////////////////////////////////////////////////////////////////////
// CMDU extension callbacks
////////////////////////////////////////////////////////////////////////////////

uint8_t CBKSend1905BBFExtensions(struct CMDU *memory_structure)
{
    if ((NULL == memory_structure) || (NULL == memory_structure->list_of_TLVs))
    {
        // Invalid arguments
        //
        return 0;
    }

    // Extend the CMDU content (more TLVs)
    //
    switch(memory_structure->message_type)
    {
        // Add non-1905 Link Metric Query TLV
        //
        case CMDU_TYPE_LINK_METRIC_QUERY:
        {
            struct linkMetricQueryTLV  *non_1905_metric_query_tlv;
            struct vendorSpecificTLV   *vendor_specific;

            // Fill non-1905 metric query TLV
            //
            non_1905_metric_query_tlv = linkMetricQueryTLVAllocAll(NULL, LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS);
            non_1905_metric_query_tlv->tlv.type             = BBF_TLV_TYPE_NON_1905_LINK_METRIC_QUERY;

            // Embed the TLV inside a BBF Vendor Specific TLV
            //
            vendor_specific = vendorSpecificTLVEmbedExtension(&non_1905_metric_query_tlv->tlv, forge_bbf_TLV_from_structure, (uint8_t *)BBF_OUI);

            // Insert the Vendor Specific TLV in CMDU
            //
            vendorSpecificTLVInsertInCDMU(memory_structure, vendor_specific);

            break;
        }

        // Add non-1905 Transmitter/Receiver Link Metric TLVs
        //
        case CMDU_TYPE_LINK_METRIC_RESPONSE:
        {
            struct transmitterLinkMetricTLV  **tx_tlvs;
            struct receiverLinkMetricTLV     **rx_tlvs;

            uint8_t total_tlvs;
            uint8_t i;

            // Insert BBF metrics only if they were requested
            //
            if (bbf_query)
            {
                struct vendorSpecificTLV          *vendor_specific;

                bbf_query = 0;

                // Fill all the needed TLVs
                //
                _obtainLocalNon1905MetricsTLV(LINK_METRIC_QUERY_TLV_ALL_NEIGHBORS, NULL, LINK_METRIC_QUERY_TLV_BOTH_TX_AND_RX_LINK_METRICS,
                                              &tx_tlvs, &rx_tlvs, &total_tlvs);

                if (NULL != tx_tlvs)
                {
                    for (i=0; i<total_tlvs; i++)
                    {
                        // Embed the TLV inside a BBF Vendor Specific TLV
                        //
                        vendor_specific = vendorSpecificTLVEmbedExtension(&tx_tlvs[i]->tlv, forge_bbf_TLV_from_structure, (uint8_t *)BBF_OUI);

                        // Insert the Vendor Specific TLV in CMDU
                        //
                        vendorSpecificTLVInsertInCDMU(memory_structure, vendor_specific);
                    }
                }
                if (NULL != rx_tlvs)
                {
                    for (i=0; i<total_tlvs; i++)
                    {
                        // Embed the TLV inside a BBF Vendor Specific TLV
                        //
                        vendor_specific = vendorSpecificTLVEmbedExtension(&rx_tlvs[i]->tlv, forge_bbf_TLV_from_structure, (uint8_t *)BBF_OUI);

                        // Insert the Vendor Specific TLV in CMDU
                        //
                        vendorSpecificTLVInsertInCDMU(memory_structure, vendor_specific);
                    }
                }

                // Free tx_tlvs and rx_tlvs (no longer used)
                //
                _freeLocalNon1905MetricsTLVs(&tx_tlvs, &rx_tlvs, &total_tlvs);
            }

            break;
        }

        // No more TLVs will be added to the CMDU
        //
        default:
        {
            break;
        }
    }

    return 1;
}


