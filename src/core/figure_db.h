// ToyPortal360 - figure name tables (generated from RPCS3) and search.
#ifndef TOYPORTAL_FIGURE_DB_H
#define TOYPORTAL_FIGURE_DB_H

#include "bytes.h"

struct SkyName
{
	u16 id;
	u16 variant;
	const char* name;
};

struct DimName
{
	u32 id;
	const char* name;
};

struct InfName
{
	u32 number;
	u8 series;
	const char* name;
};

const SkyName* sky_names(int* count);
const DimName* dim_names(int* count);        // characters (encrypted id)
const DimName* dim_token_names(int* count);  // vehicles/gadgets (plain id)
const InfName* inf_names(int* count);

const char* sky_name_for(u16 id, u16 variant); // NULL if unknown
const char* dim_name_for(u32 id);              // NULL if unknown
const char* inf_name_for(u32 number);          // NULL if unknown

// Case-insensitive substring search. Fills out[] with table indices, returns
// how many matched (at most max). Empty query matches everything.
int sky_search(const char* query, int* out, int max);
// For Dimensions, indices >= 0 are characters and indices encoded as
// -(i + 1) are vehicle/gadget tokens.
int dim_search(const char* query, int* out, int max);
int inf_search(const char* query, int* out, int max);

// Makes a file-system safe name: letters, digits, space, '-', '_' only.
void safe_file_name(const char* in, char* out, int out_size);

#endif
