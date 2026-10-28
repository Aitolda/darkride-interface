name = Lightling
type = body+head
scale = 1
filename = lightling.glb
retarget = humanoid
ownBlendshapes = true
joint = jointRoot = Hips
joint = jointLean = Spine
joint = jointNeck = Neck
joint = jointHead = Head
joint = jointEyeLeft = LeftEye
joint = jointEyeRight = RightEye
joint = jointLeftHand = LeftHand
joint = jointRightHand = RightHand
jointMap = Hips = J_Bip_C_Hips
jointMap = Spine = J_Bip_C_Spine
jointMap = Spine1 = J_Bip_C_Chest
jointMap = Spine2 = J_Bip_C_UpperChest
jointMap = Neck = J_Bip_C_Neck
jointMap = Head = J_Bip_C_Head
jointMap = LeftEye = J_Adj_L_FaceEye
jointMap = RightEye = J_Adj_R_FaceEye
jointMap = LeftShoulder = J_Bip_L_Shoulder
jointMap = LeftArm = J_Bip_L_UpperArm
jointMap = LeftForeArm = J_Bip_L_LowerArm
jointMap = LeftHand = J_Bip_L_Hand
jointMap = LeftUpLeg = J_Bip_L_UpperLeg
jointMap = LeftLeg = J_Bip_L_LowerLeg
jointMap = LeftFoot = J_Bip_L_Foot
jointMap = LeftToeBase = J_Bip_L_ToeBase
jointMap = LeftHandThumb1 = J_Bip_L_Thumb1
jointMap = LeftHandThumb2 = J_Bip_L_Thumb2
jointMap = LeftHandThumb3 = J_Bip_L_Thumb3
jointMap = LeftHandIndex1 = J_Bip_L_Index1
jointMap = LeftHandIndex2 = J_Bip_L_Index2
jointMap = LeftHandIndex3 = J_Bip_L_Index3
jointMap = LeftHandMiddle1 = J_Bip_L_Middle1
jointMap = LeftHandMiddle2 = J_Bip_L_Middle2
jointMap = LeftHandMiddle3 = J_Bip_L_Middle3
jointMap = LeftHandRing1 = J_Bip_L_Ring1
jointMap = LeftHandRing2 = J_Bip_L_Ring2
jointMap = LeftHandRing3 = J_Bip_L_Ring3
jointMap = LeftHandPinky1 = J_Bip_L_Little1
jointMap = LeftHandPinky2 = J_Bip_L_Little2
jointMap = LeftHandPinky3 = J_Bip_L_Little3
jointMap = RightShoulder = J_Bip_R_Shoulder
jointMap = RightArm = J_Bip_R_UpperArm
jointMap = RightForeArm = J_Bip_R_LowerArm
jointMap = RightHand = J_Bip_R_Hand
jointMap = RightUpLeg = J_Bip_R_UpperLeg
jointMap = RightLeg = J_Bip_R_LowerLeg
jointMap = RightFoot = J_Bip_R_Foot
jointMap = RightToeBase = J_Bip_R_ToeBase
jointMap = RightHandThumb1 = J_Bip_R_Thumb1
jointMap = RightHandThumb2 = J_Bip_R_Thumb2
jointMap = RightHandThumb3 = J_Bip_R_Thumb3
jointMap = RightHandIndex1 = J_Bip_R_Index1
jointMap = RightHandIndex2 = J_Bip_R_Index2
jointMap = RightHandIndex3 = J_Bip_R_Index3
jointMap = RightHandMiddle1 = J_Bip_R_Middle1
jointMap = RightHandMiddle2 = J_Bip_R_Middle2
jointMap = RightHandMiddle3 = J_Bip_R_Middle3
jointMap = RightHandRing1 = J_Bip_R_Ring1
jointMap = RightHandRing2 = J_Bip_R_Ring2
jointMap = RightHandRing3 = J_Bip_R_Ring3
jointMap = RightHandPinky1 = J_Bip_R_Little1
jointMap = RightHandPinky2 = J_Bip_R_Little2
jointMap = RightHandPinky3 = J_Bip_R_Little3
bs = EyeBlink_L = Fcl_EYE_Close_L = 1
bs = EyeBlink_R = Fcl_EYE_Close_R = 1
bs = EyeSquint_L = Fcl_EYE_Joy_L = 0.6
bs = EyeSquint_R = Fcl_EYE_Joy_R = 0.6
bs = EyeOpen_L = Fcl_EYE_Surprised = 0.5
bs = EyeOpen_R = Fcl_EYE_Surprised = 0.5
bs = BrowsD_L = Fcl_BRW_Angry = 0.5
bs = BrowsD_R = Fcl_BRW_Angry = 0.5
bs = BrowsU_C = Fcl_BRW_Sorrow = 1
bs = BrowsU_L = Fcl_BRW_Surprised = 0.5
bs = BrowsU_R = Fcl_BRW_Surprised = 0.5
bs = JawOpen = Fcl_MTH_A = 1
bs = MouthFrown_L = Fcl_MTH_Sorrow = 0.5
bs = MouthFrown_R = Fcl_MTH_Sorrow = 0.5
bs = MouthSmile_L = Fcl_MTH_Joy = 0.5
bs = MouthSmile_R = Fcl_MTH_Joy = 0.5
bs = LipsStretch_L = Fcl_MTH_I = 0.4
bs = LipsStretch_R = Fcl_MTH_I = 0.4
bs = LipsFunnel = Fcl_MTH_O = 1
bs = LipsPucker = Fcl_MTH_U = 1
materialMap = {"mat::LightlingBody":"lightling-body.json","mat::LightlingFace":"lightling-face.json","mat::LightlingHairShort":"lightling-hair-short.json","mat::LightlingHairTwintails":"lightling-hair-twintails.json"}
avatarMenu = {"version":1,"parameters":[{"name":"Hair","type":"int","default":0,"saved":true,"synced":true}],"menu":{"controls":[{"name":"Hair","type":"submenu","controls":[{"name":"Short","type":"toggle","parameter":"Hair","value":0},{"name":"Twin tails","type":"toggle","parameter":"Hair","value":1},{"name":"None","type":"toggle","parameter":"Hair","value":2}]}]},"objects":[{"path":"HairShort","parts":[10]},{"path":"HairTwintails","parts":[11]}],"layers":[{"name":"Hair","default":0,"states":[{"name":"Short","effect":{"active":[[0,true],[1,false]],"blendshapes":{"TwintailsShow":0}}},{"name":"Twin tails","effect":{"active":[[0,false],[1,true]],"blendshapes":{"TwintailsShow":1}}},{"name":"None","effect":{"active":[[0,false],[1,false]],"blendshapes":{"TwintailsShow":0}}}],"anyState":[{"to":0,"conditions":[["Hair","equals",0]],"duration":0},{"to":1,"conditions":[["Hair","equals",1]],"duration":0},{"to":2,"conditions":[["Hair","equals",2]],"duration":0}]}]}
