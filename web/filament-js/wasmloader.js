/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/// LoadFilament ::function:: Loads the Filament Module and optionally downloads assets.
///
/// All JavaScript clients must call LoadFilament function, passing in a list of asset URL's and a
/// callback. This callback gets invoked only after all assets have been downloaded and the Filament
/// WebAssembly Module has been loaded. Clients should only pass asset URL's that absolutely must
/// be ready at initialization time.
///
/// When the callback is called, each downloaded asset is available in the `Filament.assets` global
/// object, which contains a mapping from URL's to Uint8Array objects.
///
/// args.assetUris ::argument:: Array of strings containing URL's of required assets.
/// args.onReady ::argument:: callback that gets invoked after all assets have been downloaded
/// and the Filament WebAssembly Module has been loaded.

Module.postRun = () => {

    // TypeScript clients treat the module as a namespace, so we assign it to a global symbol.
    window['Filament'] = Module;

    // Add handwritten JavaScript wrappers into the module.
    Module.loadClassExtensions();

    // Usage of glmatrix is optional. If it exists, then go ahead and augment it with some
    // useful math functions.
    if (typeof glMatrix !== 'undefined') {
        Module.loadMathExtensions();
    }

    // This will hold the URI => Uint8Array map for downloaded assets.
    Module.assets = Module.assets || {};

    // Issue a fetch for each URI string.
    const onReady = Module.onReady || (() => {});
    const uris = Module.assetUris;
    if (!uris) {
        onReady();
        return;
    }
    let remainingTasks = uris.length;
    Module.fetch(uris, null, () => {
        if (--remainingTasks == 0) {
            onReady();
        }
    });
};

/// fetch ::function:: Downloads assets and invokes a callback when done.
///
/// This utility consumes an array of URI strings and invokes callbacks after each asset is
/// downloaded. Additionally, each downloaded asset becomes available in the `Module.assets`
/// global object, which is a mapping from URI strings to `Uint8Array`. If desired, clients can
/// pre-populate entries in `Filament.assets` to circumvent HTTP requests.
///
/// This function is used internally by `LoadFilament` and `gltfio$FilamentAsset.loadResources`.
///
/// assetUris ::argument:: Array of strings containing URL's of required assets.
/// onDone ::argument:: callback that gets invoked after all assets have been downloaded.
/// onFetched ::argument:: optional callback that's invoked after each asset is downloaded.
Module.fetch = (assetUris, onDone, onFetched) => {
    let remainingAssets = assetUris.length;
    assetUris.forEach(name => {

        // Check if a buffer already exists in case the client wishes
        // to provide its own data rather than using a HTTP request.
        if (Module.assets[name]) {
            if (onFetched) {
                onFetched(name);
            }
            if (--remainingAssets === 0 && onDone) {
                onDone();
            }
        } else {
            fetch(name).then(response => {
                if (!response.ok) {
                    throw new Error(name);
                }
                return response.arrayBuffer();
            }).then(arrayBuffer => {
                Module.assets[name] = new Uint8Array(arrayBuffer);
                if (onFetched) {
                    onFetched(name);
                }
                if (--remainingAssets === 0 && onDone) {
                    onDone();
                }
            });
        }
    });
};
