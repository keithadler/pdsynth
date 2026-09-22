/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * macOS MIDI input. Publishes a virtual destination named "pdsynth", so a DAW
 * or a utility can send to it, and also connects every physical source that is
 * present, so a keyboard plugged in just works.
 */
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>
#include "pd_midi.h"

static MIDIClientRef   s_client;
static MIDIPortRef     s_port;
static MIDIEndpointRef s_virtual;
static pd_midi_fn      s_fn;
static void           *s_user;
static char            s_name[128];

static void read_proc(const MIDIPacketList *pkts, void *a, void *b)
{
    (void)a; (void)b;
    if (!s_fn) return;
    const MIDIPacket *p = &pkts->packet[0];
    for (UInt32 i = 0; i < pkts->numPackets; i++) {
        s_fn(s_user, p->data, p->length);
        p = MIDIPacketNext(p);
    }
}

const char *pd_midi_open(pd_midi_fn fn, void *user)
{
    s_fn = fn; s_user = user;
    if (MIDIClientCreate(CFSTR("pdsynth"), NULL, NULL, &s_client) != noErr) return NULL;
    MIDIDestinationCreate(s_client, CFSTR("pdsynth"), read_proc, NULL, &s_virtual);
    if (MIDIInputPortCreate(s_client, CFSTR("in"), read_proc, NULL, &s_port) != noErr) return NULL;

    ItemCount n = MIDIGetNumberOfSources();
    int attached = 0;
    for (ItemCount i = 0; i < n; i++) {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (!src) continue;
        if (MIDIPortConnectSource(s_port, src, NULL) == noErr) {
            attached++;
            if (s_name[0] == 0) {
                CFStringRef nm = NULL;
                MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &nm);
                if (nm) {
                    CFStringGetCString(nm, s_name, sizeof s_name, kCFStringEncodingUTF8);
                    CFRelease(nm);
                }
            }
        }
    }
    if (!attached) snprintf(s_name, sizeof s_name, "pdsynth (virtual)");
    return s_name;
}

void pd_midi_close(void)
{
    s_fn = NULL;
    if (s_port)    MIDIPortDispose(s_port), s_port = 0;
    if (s_virtual) MIDIEndpointDispose(s_virtual), s_virtual = 0;
    if (s_client)  MIDIClientDispose(s_client), s_client = 0;
}
