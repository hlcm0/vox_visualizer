from __future__ import annotations

VOL_L_TRACK = 1
FX_L_TRACK = 2
BT_A_TRACK = 3
BT_B_TRACK = 4
BT_C_TRACK = 5
BT_D_TRACK = 6
FX_R_TRACK = 7
VOL_R_TRACK = 8

LASER_TRACKS = (VOL_L_TRACK, VOL_R_TRACK)
FX_TRACKS = (FX_L_TRACK, FX_R_TRACK)
BT_TRACKS = (BT_A_TRACK, BT_B_TRACK, BT_C_TRACK, BT_D_TRACK)
BUTTON_TRACKS = FX_TRACKS + BT_TRACKS
TRACK_IDS = LASER_TRACKS[:1] + BUTTON_TRACKS + LASER_TRACKS[1:]

BT_COLUMN_INDEX_BY_TRACK = {
    BT_A_TRACK: 0,
    BT_B_TRACK: 1,
    BT_C_TRACK: 2,
    BT_D_TRACK: 3,
}


def is_laser_track(track: int) -> bool:
    return track in LASER_TRACKS


def is_fx_track(track: int) -> bool:
    return track in FX_TRACKS


def is_bt_track(track: int) -> bool:
    return track in BT_TRACKS


def is_button_track(track: int) -> bool:
    return track in BUTTON_TRACKS
