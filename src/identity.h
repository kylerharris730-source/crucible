#pragma once
#include "common.h"

/* --- who this player is, across sessions ------------------------------------

   A random 128-bit value, written once and read forever after, so that a host
   can recognise somebody rejoining and hand back the pack they left with.

   It is NOT derived from the machine, and that is a decision rather than an
   oversight. A MAC address looks like the obvious answer and is wrong four
   times over: it is a hardware identifier and therefore a privacy problem in a
   game children play, it is trivially spoofable and therefore not a security
   boundary, it is not even stable -- wifi, ethernet, a dock, a VPN and a new
   adapter all change it -- and a machine has several, so "the" address is
   ambiguous. Every one of those failures ends with a player losing their
   character, which is the exact thing this exists to prevent.

   A random number in a file has none of those problems. It identifies nobody,
   it survives hardware changes, and the worst case is that someone who deletes
   it starts a new character. It comes from the operating system's
   cryptographic generator, because a host hands a remembered pack back to
   whoever presents it, and a guessable one could be claimed.

   Stored under the USER rather than beside the executable, so the game can be
   moved, reinstalled or launched from anywhere and still be you.

   This is an identity, not a credential. It travels in the clear and anyone
   who has it can claim that character on a host that remembers it -- which is
   the same trust model as telling your friend the port number, and is fine for
   the people you already invited. It should not become a login. */
const char* playerIdentity();

/* 32 hex characters plus a terminator. */
static const int PLAYER_IDENTITY_CHARS = 32;

/* A fresh identity into `out` (PLAYER_IDENTITY_CHARS + 1), without touching
   the file. playerIdentity() uses it for a first launch; tests use it so they
   never read or replace the real player's identity. */
void identityGenerate(char* out);
