FilamentApp Next

# Objective

We wish to rewrite Filament's demo infrastructure and glTF viewer to use a client-server
architecture. Motivations include:

- Transitioning away from desktop OpenGL.
- Allowing for easy parameter tweaking on Android.
- Reducing inconsistent and duplicated demo code across platforms.
- Encouraging the team to perform more testing and iteration on mobile devices.
- Enabling drag-and-drop for glTF models and IBL environments.

One non-goal for this project is to build a production-ready API for client applications.

# Background

Filament is an extremely cross platform rendering library. It includes sample code for Android,
desktop platforms, iOS, and WebGL. The reality however is that we (the Filament team) most often
perform testing and iteration on macOS, which is not our primary target platform.

Another problem is that our small team maintains several backends (Desktop OpenGL, OpenGL ES,
Vulkan, and Metal) and more backends are on the way (WebGPU / Dawn). Desktop OpenGL is arguably the
least important backend, yet it is what we test against most frequently. Dropping it would not only
reduce our maintenance burden, it would force us to focus our engineering efforts in a better way.

We recently built a tool called matdbg for internal use, and its client / server architecture has
been successful at enhancing our productivity. For this project we may wish to extend matdbg or
re-use some of its ideas. More on this later in the doc.

Filament already includes a library called "libs/filamentapp", which is what we seek to replace.
This library has several problems:

- It exists only for our desktop samples.
- It has grown organically to become a bit of a mess.
- It uses SDL2. This should be replaced with GLFW, which is more modern and focused.
- It has a 16 ms sleep that makes it a poor testbed for performance experiments.

# Design questions and answers

*Should "FilamentApp Next" share code with matdbg, since they are both client-server?*

This idea would mitigate bitrot in matdbg, and the success of matdbg might indicate that its tech
stack / architecture would work well in future endeavors.

However I do not think that extending the matdbg codebase is the right course of action. Its
communication protocol is a handwritten mix of HTTP requests and WebSocket messages; this would
difficult to maintain if it had a larger scope. Its UI code is very limited, composed of raw HTML,
CSS, and moustache templates. These technologies are not aligned with our team's skillset.

Moreover matdbg is designed to be intrusive into the Filament core renderer, allowing debugging of
any application. FilamentApp Next needs first-class support for glTF in its client / server
architecture, so it should therefore be external to the Filament core.

*Should the UI be web-based like matdbg or should it be ImGui-based like gltf_viewer?*

The advantage of a web-based client is that there would be nothing to build on the client side;
you would just point your browser to the appropriate localhost URL. If your machine doesn't have
a firewall, you could even point a remote colleague to an IP address to give them remote access.

If we were to build a web-based client, it would be best to devise a system that avoids maintaining
HTML / CSS / JS, since these technologies are not our team's core competency. Simple solutions
already exist that solve this problem, such as **datGUI**. This is a Google project that can consume
simple JSON specification for the UI. It is similar to ImGui in some its design goals. Please see
the following demo:

http://workshop.chromeexperiments.com/examples/gui

However, there are several benefits to continuing the use of ImGui:
- We've already built several ImGui extensions that would be nice to keep using.
- The expertise on the team is not aligned with web development.
- We've already come up to speed with ImGui and we like it.

Given all this, I am leaning towards simply continuing to use ImGui instead of building a web
client. At a later point, we can perhaps add a web client that uses the same client-server protocol.

*Should the client app perform any 3D rendering?*

One can imagine a mirror style of operation where the client controller uses Filament to draw the
same image that is being rendered on the remote device. I do not see much use for this, and it would
add a layer of complexity that I do not think is justified. Note that if we not have a mirror mode,
we should at least ensure that the remote touch interface supports zoom, pan, and spin.

# Tentative design details

- We will introduce libs/gltfserver (which in turns uses civetweb), as well as two apps that link
  against it: one for Android and one for Desktop. iOS and WebGL support can come later.
- libs/filamentapp is rewritten (does not depend on libs/gltfserver).
- samples/gltf_viewer is vastly simplified, and becomes an example app with no UI.
- tools/gltf_client is introduced, with two mockups below.

The left mockup shows its initial state, the right mockup shows what it looks like after the
user asks to connect to localhost.

[IMAGE](IMAGE)

# Settings JSON

cgltf works well, JSON is easy...

# Outline of work

TODO