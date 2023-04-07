#ifndef MOBAGEN_SCRIPTEDBEHAVIOUR_H
#define MOBAGEN_SCRIPTEDBEHAVIOUR_H

#include "../error/NotImplemented.h"
#include "Behaviour.h"
#include <string>
#include <SDL.h>

// equivalent to monobeaviour in unity
class ScriptedBehaviour : public Behaviour {
  // public methods
  bool IsInvoking() { throw NotImplemented(); }
  void CancelInvoke() { throw NotImplemented(); }
  void Invoke(std::string methodName, float time) { throw NotImplemented(); }
  void InvokeRepeating(std::string methodName, float time, float repeatRate) { throw NotImplemented(); }
  void CancelInvoke(std::string methodName) { throw NotImplemented(); }
  bool IsInvoking(std::string methodName) { throw NotImplemented(); }
  // Coroutine StartCoroutine(std::string methodName);
  // Coroutine StartCoroutine(string methodName, object value == nullptr);
  // Coroutine StartCoroutine(IEnumerator routine);
  // void StopCoroutine(IEnumerator routine);
  void StopCoroutine(std::string methodName) { throw NotImplemented(); };
  void StopAllCoroutines() { throw NotImplemented(); };

  // static
  static void print(std::string message) { SDL_Log("%s", message.c_str()); };


  // https://docs.unity3d.com/ScriptReference/MonoBehaviour.html
  // messages
  void 	Awake	(){ throw NotImplemented(); }
  void 	FixedUpdate	(){ throw NotImplemented(); }
  void 	LateUpdate	(){ throw NotImplemented(); }
//  void 	OnAnimatorIK	(){ throw NotImplemented(); }
//  void 	OnAnimatorMove	(){ throw NotImplemented(); }
//  void 	OnApplicationFocus	(){ throw NotImplemented(); }
//  void 	OnApplicationPause	(){ throw NotImplemented(); }
//  void 	OnApplicationQuit	(){ throw NotImplemented(); }
//  void 	OnAudioFilterRead	(){ throw NotImplemented(); }
//  void 	OnBecameInvisible	(){ throw NotImplemented(); }
//  void 	OnBecameVisible	(){ throw NotImplemented(); }
//  void 	OnCollisionEnter	(){ throw NotImplemented(); }
//  void 	OnCollisionEnter2D	(){ throw NotImplemented(); }
//  void 	OnCollisionExit	(){ throw NotImplemented(); }
//  void 	OnCollisionExit2D	(){ throw NotImplemented(); }
//  void 	OnCollisionStay	(){ throw NotImplemented(); }
//  void 	OnCollisionStay2D	(){ throw NotImplemented(); }
//  void 	OnConnectedToServer	(){ throw NotImplemented(); }
//  void 	OnControllerColliderHit	(){ throw NotImplemented(); }
  void 	OnDestroy	(){ throw NotImplemented(); }
  void 	OnDisable	(){ throw NotImplemented(); }
//  void 	OnDisconnectedFromServer	(){ throw NotImplemented(); }
//  void 	OnDrawGizmos	(){ throw NotImplemented(); }
//  void 	OnDrawGizmosSelected	(){ throw NotImplemented(); }
  void 	OnEnable	(){ throw NotImplemented(); }
//  void 	OnFailedToConnect	(){ throw NotImplemented(); }
//  void 	OnFailedToConnectToMasterServer	(){ throw NotImplemented(); }
  void 	OnGUI	(){ throw NotImplemented(); }
//  void 	OnJointBreak	(){ throw NotImplemented(); }
//  void 	OnJointBreak2D	(){ throw NotImplemented(); }
//  void 	OnMasterServerEvent	(){ throw NotImplemented(); }
//  void 	OnMouseDown	(){ throw NotImplemented(); }
//  void 	OnMouseDrag	(){ throw NotImplemented(); }
//  void 	OnMouseEnter	(){ throw NotImplemented(); }
//  void 	OnMouseExit	(){ throw NotImplemented(); }
//  void 	OnMouseOver	(){ throw NotImplemented(); }
//  void 	OnMouseUp	(){ throw NotImplemented(); }
//  void 	OnMouseUpAsButton	(){ throw NotImplemented(); }
//  void 	OnNetworkInstantiate	(){ throw NotImplemented(); }
//  void 	OnParticleCollision	(){ throw NotImplemented(); }
//  void 	OnParticleSystemStopped	(){ throw NotImplemented(); }
//  void 	OnParticleTrigger	(){ throw NotImplemented(); }
//  void 	OnParticleUpdateJobScheduled	(){ throw NotImplemented(); }
//  void 	OnPlayerConnected	(){ throw NotImplemented(); }
//  void 	OnPlayerDisconnected	(){ throw NotImplemented(); }
//  void 	OnPostRender	(){ throw NotImplemented(); }
//  void 	OnPreCull	(){ throw NotImplemented(); }
//  void 	OnPreRender	(){ throw NotImplemented(); }
//  void 	OnRenderImage	(){ throw NotImplemented(); }
//  void 	OnRenderObject	(){ throw NotImplemented(); }
//  void 	OnSerializeNetworkView	(){ throw NotImplemented(); }
//  void 	OnServerInitialized	(){ throw NotImplemented(); }
//  void 	OnTransformChildrenChanged	(){ throw NotImplemented(); }
//  void 	OnTransformParentChanged	(){ throw NotImplemented(); }
//  void 	OnTriggerEnter	(){ throw NotImplemented(); }
//  void 	OnTriggerEnter2D	(){ throw NotImplemented(); }
//  void 	OnTriggerExit	(){ throw NotImplemented(); }
//  void 	OnTriggerExit2D	(){ throw NotImplemented(); }
//  void 	OnTriggerStay	(){ throw NotImplemented(); }
//  void 	OnTriggerStay2D	(){ throw NotImplemented(); }
//  void 	OnValidate	(){ throw NotImplemented(); }
//  void 	OnWillRenderObject	(){ throw NotImplemented(); }
  void 	Reset	(){ throw NotImplemented(); }
  void 	Start	(){ throw NotImplemented(); }
  void 	Update	(){ throw NotImplemented(); }

  // static
  static void 	Destroy	(){ throw NotImplemented(); }
  static void 	DestroyImmediate	(){ throw NotImplemented(); }
  static void 	DontDestroyOnLoad	(){ throw NotImplemented(); }
  static void 	FindAnyObjectByType	(){ throw NotImplemented(); }
  static void 	FindFirstObjectByType	(){ throw NotImplemented(); }
  static void 	FindObjectOfType	(){ throw NotImplemented(); }
  static void 	FindObjectsByType	(){ throw NotImplemented(); }
  static void 	FindObjectsOfType	(){ throw NotImplemented(); }
  static void 	Instantiate	(){ throw NotImplemented(); }

  // operators
  // todo: equality operators
};

#endif  // MOBAGEN_SCRIPTEDBEHAVIOUR_H
