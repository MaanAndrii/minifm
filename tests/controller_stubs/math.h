#pragma once
#include <float.h>
double fabs(double v);
double floor(double v);
double sqrt(double v);
#define isnan(v) ((v)!=(v))
#define isinf(v) ((v)>DBL_MAX || (v)<-DBL_MAX)
