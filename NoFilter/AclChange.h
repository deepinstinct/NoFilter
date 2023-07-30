#pragma once

#include "Utils.h"
#include <aclapi.h>

#define GENERIC_ACCESS (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)

void AdjustDesktop();
