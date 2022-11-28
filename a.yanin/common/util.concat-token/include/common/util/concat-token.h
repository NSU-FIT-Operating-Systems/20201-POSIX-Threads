#pragma once

#define CONCAT_EXPANDED(lhs, rhs) lhs ## rhs
#define CONCAT(lhs, rhs) CONCAT_EXPANDED(lhs, rhs)
