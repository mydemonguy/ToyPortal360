// ToyPortal360 - figure name tables and search.
#include "figure_db.h"

static const SkyName kSkyNames[] = {
#include "names_skylanders.inc"
};

static const DimName kDimNames[] = {
#include "names_dimensions.inc"
};

static const DimName kDimTokens[] = {
#include "names_dimensions_tokens.inc"
};

static const InfName kInfNames[] = {
#include "names_infinity.inc"
};

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

const SkyName* sky_names(int* count) { *count = COUNT_OF(kSkyNames); return kSkyNames; }
const DimName* dim_names(int* count) { *count = COUNT_OF(kDimNames); return kDimNames; }
const DimName* dim_token_names(int* count) { *count = COUNT_OF(kDimTokens); return kDimTokens; }
const InfName* inf_names(int* count) { *count = COUNT_OF(kInfNames); return kInfNames; }

const char* inf_name_for(u32 number)
{
	for (int i = 0; i < COUNT_OF(kInfNames); i++)
		if (kInfNames[i].number == number)
			return kInfNames[i].name;
	return NULL;
}

const char* sky_name_for(u16 id, u16 variant)
{
	const char* base = NULL;
	for (int i = 0; i < COUNT_OF(kSkyNames); i++)
	{
		if (kSkyNames[i].id != id)
			continue;
		if (kSkyNames[i].variant == variant)
			return kSkyNames[i].name;
		if (kSkyNames[i].variant == 0)
			base = kSkyNames[i].name;
	}
	return base;
}

const char* dim_name_for(u32 id)
{
	for (int i = 0; i < COUNT_OF(kDimNames); i++)
		if (kDimNames[i].id == id)
			return kDimNames[i].name;
	for (int i = 0; i < COUNT_OF(kDimTokens); i++)
		if (kDimTokens[i].id == id)
			return kDimTokens[i].name;
	return NULL;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static bool contains_ci(const char* hay, const char* needle)
{
	if (!needle || !*needle)
		return true;
	for (; *hay; hay++)
	{
		const char* h = hay;
		const char* n = needle;
		while (*h && *n && lower(*h) == lower(*n))
		{
			h++;
			n++;
		}
		if (!*n)
			return true;
	}
	return false;
}

int sky_search(const char* query, int* out, int max)
{
	int n = 0;
	for (int i = 0; i < COUNT_OF(kSkyNames) && n < max; i++)
		if (contains_ci(kSkyNames[i].name, query))
			out[n++] = i;
	return n;
}

int dim_search(const char* query, int* out, int max)
{
	int n = 0;
	for (int i = 0; i < COUNT_OF(kDimNames) && n < max; i++)
		if (contains_ci(kDimNames[i].name, query)) // includes "Blank Tag" (id 0)
			out[n++] = i;
	for (int i = 0; i < COUNT_OF(kDimTokens) && n < max; i++)
		if (contains_ci(kDimTokens[i].name, query))
			out[n++] = -(i + 1);
	return n;
}

int inf_search(const char* query, int* out, int max)
{
	int n = 0;
	for (int i = 0; i < COUNT_OF(kInfNames) && n < max; i++)
		if (contains_ci(kInfNames[i].name, query))
			out[n++] = i;
	return n;
}

void safe_file_name(const char* in, char* out, int out_size)
{
	int n = 0;
	for (; *in && n < out_size - 1; in++)
	{
		const char c = *in;
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		                c == ' ' || c == '-' || c == '_';
		out[n++] = ok ? c : '_';
	}
	// FATX file names are limited to 42 characters.
	if (n > 37)
		n = 37;
	out[n] = 0;
}
