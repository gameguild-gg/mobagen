// ref https://docs.unity3d.com/ScriptReference/HideFlags.html
// If you set hide flags to DontSaveInEditor, DontSaveInBuild or HideInHierarchy, this internally removes the object from the Unity Scene, as well as from its current physics scene (this includes both 2D and 3D physics scenes). This also causes the object to trigger its OnDisable and OnEnable calls.
enum class HideFlags{
  // A normal, visible object. This is the default.
  None=0,
  // The object will not appear in the hierarchy.
  HideInHierarchy=1,
  // It is not possible to view it in the inspector.
  HideInInspector=2,
  // The object will not be saved to the Scene in the editor.
  DontSaveInEditor=4,
  // The object is not editable in the Inspector.
  NotEditable=8,
  // The object will not be saved when building a player.
  DontSaveInBuild=16,
  // The object will not be unloaded by Resources.UnloadUnusedAssets.
  DontUnloadUnusedAsset = 32,
  // The object will not be saved to the Scene. It will not be destroyed when a new Scene is loaded. It is a shortcut for HideFlags.DontSaveInBuild | HideFlags.DontSaveInEditor | HideFlags.DontUnloadUnusedAsset.
  DontSave = 52,
  // The GameObject is not shown in the Hierarchy, not saved to Scenes, and not unloaded by Resources.UnloadUnusedAssets.
  HideAndDontSave=128
};