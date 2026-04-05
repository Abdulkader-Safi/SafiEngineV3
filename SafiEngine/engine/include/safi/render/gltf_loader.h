/**
 * safi/render/gltf_loader.h — load a glTF model into a SafiMesh (+texture).
 *
 * This is a minimal loader: it walks the first mesh's first primitive,
 * extracts POSITION / NORMAL / TEXCOORD_0 / indices, and uploads them to the
 * GPU. If the primitive has a base-color texture it is also uploaded into
 * the provided material.
 */
#ifndef SAFI_RENDER_GLTF_LOADER_H
#define SAFI_RENDER_GLTF_LOADER_H

#include <stdbool.h>

#include "safi/render/renderer.h"
#include "safi/render/mesh.h"
#include "safi/render/material.h"

bool safi_gltf_load(SafiRenderer *r,
                    const char   *path,
                    SafiMesh     *out_mesh,
                    SafiMaterial *inout_material);

#endif /* SAFI_RENDER_GLTF_LOADER_H */
