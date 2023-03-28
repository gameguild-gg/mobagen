# Architecture

![Architecture](assets/architecture.svg)


https://playground.diagram.codes/d/system-layers

```
V[core,engine,editor] with label "Architecture"

core=H["Math","Containers &\nData Converters","Log",platform] with label "Core"
platform=H[mobile,desktop,web] with label "Platform Specific"

mobile=H["android","ios"] with label "Mobile"
desktop=H["win","osx","linux"] with label "Desktop"
web=H["wasm"] with label "WEB"

engine=H[orchestrator,rendering,physics,resource,animation,networking,gameplay] with label "Engine Modules"
rendering=V["GUI","Rendering","Shaders","Scripting","Materials"] with label "Graphics"

physics=V["Particles","Collision","RigidBody","Constraints"] with label "Physics"
animation=V["Skeleton","Poses","Clip","Skinning","Blending","Sprite"] with label "Animation"
orchestrator=V["Time\nManager","Job\nScheduler","IO","Predict\nRollback", "Plugin\nManager"] with label "Orchestrator"

gameplay=V["Scripting","Scene\nGraph","ECS","Events &\nMessaging","AI"] with label "Gameplay"
networking=V["HTTPs","Real-Time"] with label "Networking"
resource=V["Mesh","Audio","Video","Image","Text","Font","Binary","Prefab"] with label "Resource\nManager"

editor=V[editor1] with label "Editor"
editor1=H["Simulator","Visual Scripting","Scene Editor","Animator"]
```
