#include "../src/game/CharacterData.hpp"
#include "../src/game/ArsenalData.hpp"
#include "../src/game/CharacterWeaponData.hpp"
#include <fstream>
#include <iostream>
#include <set>

int main(int argc,char** argv){
    using namespace restoration;
    int failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}};
    try{
        CharacterData data(argc>1?argv[1]:"assets/reference/characters");
        check(data.rigCount()==128 && data.models().size()==411,"complete source rig/model roster");
        std::set<std::string> meshes;
        for(int hair=0;hair<8;++hair)for(int hat=0;hat<4;++hat)for(int brows=0;brows<4;++brows){
            const auto& rig=data.rig(hair,hat,brows);
            check(rig.bones[0].name=="hips" && rig.bones[4].name=="head","source bone order");
            check(rig.hair.size()<=8,"source hair strand limit");
            for(const auto* lod:{"hero","game","far"}){
                const auto& model=data.model("body/"+std::to_string(hair)+"/"+std::to_string(hat)+"/"+std::to_string(brows)+"/"+lod);
                bool skin=false,cloth=false,eyes=false;
                for(const auto& node:model.nodes){skin|=node.material=="skin";cloth|=node.material=="cloth";eyes|=node.material=="eye";}
                check(skin&&cloth&&eyes,"source body has skin/cloth/eyes at every LOD");
            }
        }
        for(const auto& entry:MainEquipment())check(!data.model("main/"+std::string(entry.id)).nodes.empty(),"all 12 main source models");
        for(const auto& entry:SubEquipment())check(!data.model("sub/"+std::string(entry.id)).nodes.empty(),"all 15 sub source models");
        bool canopyMatrix=false,canopyInk=false,canopyCream=false,bladeEdge=false,bladeAura=false,sponge=false;
        for(const auto& node:data.model("main/brolly").nodes){
            canopyMatrix|=node.local.matrixOverride;
            canopyInk|=node.material=="brollyInk";canopyCream|=node.material=="brollyCream";
        }
        for(const auto& node:data.model("main/blade").nodes){bladeEdge|=node.material=="bladeEdge";bladeAura|=node.material=="bladeAura";}
        for(const auto& node:data.model("main/mitts").nodes)sponge|=node.material=="sponge";
        check(canopyMatrix&&canopyInk&&canopyCream,"source canopy authored matrices and both materials preserved");
        check(bladeEdge&&bladeAura&&sponge,"source kit custom material geometry preserved");
        for(const auto& weapon:CharacterWeaponRigs){
            const auto product=weapon.handR.matrix()*weapon.inHand.matrix();
            double error=0;for(int c=0;c<4;++c)for(int r=0;r<4;++r)error=std::max(error,std::abs(product[c][r]-(c==r?1.0:0.0)));
            check(error<1e-12,"source right-hand weapon grip inverse preserved");
        }
        bool unknownGrip=false;try{GetCharacterWeaponRig("unknown");}catch(const std::invalid_argument&){unknownGrip=true;}
        check(unknownGrip,"unknown weapon grip rejected");
        check(CharacterWeaponHolds.size()==MainEquipment().size(),"complete source weapon hold roster");
        for(const auto& weapon:MainEquipment())check(GetCharacterWeaponHold(weapon.id).kind==weapon.kind,"source hold uses canonical weapon kind");
        check(GetCharacterWeaponHold("bow").animKind=="charger"&&GetCharacterWeaponHold("twins").animKind=="shooter","source borrowed hold families preserved");
        check(GetCharacterWeaponHold("roller").roll.has_value()&&GetCharacterWeaponHold("shooter").run.has_value()&&GetCharacterWeaponHold("mitts").dual,"source optional holds and dual-fist flag preserved");
        unknownGrip=false;try{GetCharacterWeaponHold("unknown");}catch(const std::invalid_argument&){unknownGrip=true;}
        check(unknownGrip,"unknown weapon hold rejected");
        for(const auto& [id,model]:data.models())for(const auto& node:model.nodes)if(!node.mesh.empty())meshes.insert(node.mesh);
        std::size_t triangles=0;
        for(const auto& file:meshes){
            const auto& mesh=data.mesh(file);triangles+=mesh.indices.size()/3;
            check(mesh.vertices>0 && mesh.indices.size()%3==0,"valid source triangles");
            const CharacterAttribute* weights=nullptr;const CharacterAttribute* joints=nullptr;
            for(const auto& attr:mesh.attributes){if(attr.name=="skinWeight")weights=&attr;if(attr.name=="skinIndex")joints=&attr;}
            if(joints)for(float index:joints->values)check(index>=0 && index<87,"source skin joint range");
            if(weights)for(std::size_t i=0;i<weights->values.size();i+=4){
                const float total=weights->values[i]+weights->values[i+1]+weights->values[i+2]+weights->values[i+3];
                check(total>0.9999f && total<1.0001f,"source skin weights normalized");
            }
        }
        bool rejected=false;try{data.mesh("../escape.mesh");}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"mesh paths cannot escape asset directory");
        rejected=false;try{data.rig(8,0,0);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"unknown style index rejected");
        std::cout<<data.rigCount()<<" rigs / "<<data.models().size()<<" models / "<<meshes.size()<<" meshes / "<<triangles<<" source triangles validated\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    return failures?1:0;
}
