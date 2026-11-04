#ifndef MIC16_HLS_GEOMETRY_CONVENTION_H
#define MIC16_HLS_GEOMETRY_CONVENTION_H

#define MIC16_GEOMETRY_CONTRACT_VERSION "mic16_geometry_channel_lag_contract_v1"
#define MIC16_CHANNEL_MAP_CONVENTION "channel_map[logical_mic_id]=udp_raw_channel_id"
#define MIC16_ANGLE_DEFINITION "array_center_to_source"
#define MIC16_ANGLE_ZERO "MIC0/+X"
#define MIC16_ANGLE_POSITIVE "counter_clockwise"
#define MIC16_LAG_ORDER "ascending_-max_to_+max"
#define MIC16_LAG_POSITIVE_MEANING "for pair(a,b), channel b arrives later than channel a"
#define MIC16_LAG_INTERPOLATION_METHOD "linear_fractional_lag"

static const unsigned char MIC16_GEOMETRY_PAIRS[32][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 4},
    {4, 5}, {5, 6}, {6, 7}, {7, 8},
    {8, 9}, {9, 10}, {10, 11}, {11, 12},
    {12, 13}, {13, 14}, {14, 15}, {15, 0},
    {0, 8}, {1, 9}, {2, 10}, {3, 11},
    {4, 12}, {5, 13}, {6, 14}, {7, 15},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
    {4, 8}, {5, 9}, {6, 10}, {7, 11}
};

#define MIC16_HLS_PAIRS MIC16_GEOMETRY_PAIRS

#endif
