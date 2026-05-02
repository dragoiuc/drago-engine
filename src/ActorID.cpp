#include "ActorID.h"

static int g_nextActorID = 0;

int ActorID::Next() {
    return g_nextActorID++;
}