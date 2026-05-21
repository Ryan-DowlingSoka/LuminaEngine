#pragma once

// Shared froxel-volume math for the volumetric fog passes. Pure functions only --
// deliberately does NOT include SceneGlobals so the integrate pass can use it
// without pulling set-0 bindings it doesn't bind.

// Positive linear view distance (view-space Z magnitude) at froxel slice boundary
// `SliceF` (may be fractional) for an exponential near..range distribution. Mirrors
// the cluster Z slicing so froxels align with the clustered light grid.
float Froxel_SliceDistance(float SliceF, float GridZ, float NearP, float Range)
{
    return NearP * pow(Range / NearP, SliceF / GridZ);
}

// Inverse of the slice distribution: positive view distance -> normalized froxel W [0,1].
float Froxel_DistanceToW(float ViewDist, float NearP, float Range)
{
    return saturate(log(max(ViewDist, NearP) / NearP) / log(Range / NearP));
}
