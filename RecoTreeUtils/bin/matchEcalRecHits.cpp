#include <TH1F.h>
#include <TMath.h>
#include <TROOT.h>
#include <TFile.h>
#include <TSystem.h>

#include "DataFormats/FWLite/interface/Event.h"
#include "DataFormats/FWLite/interface/Handle.h"
#include "DataFormats/JetReco/interface/PFJet.h"
#include "DataFormats/FWLite/interface/InputSource.h"
#include "DataFormats/FWLite/interface/OutputFiles.h"
#include "DataFormats/Common/interface/SortedCollection.h"
#include "DataFormats/EcalRecHit/interface/EcalRecHit.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidate.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecTrack.h"
#include "DataFormats/ParticleFlowReco/interface/PFCluster.h"
#include "DataFormats/ParticleFlowReco/interface/PFBlock.h"
#include "DataFormats/ParticleFlowReco/interface/PFBlockElement.h"
#include "FWCore/FWLite/interface/AutoLibraryLoader.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/PythonParameterSet/interface/MakeParameterSets.h"
#include "PhysicsTools/FWLite/interface/TFileService.h"

using namespace std;

typedef enum sub_det {
    NONE=0,
    TRACK=1,
    PS1=2,
    PS2=3,
    ECAL=4,
    HCAL=5,
    GSF=6,
    BREM=7,
    HFEM=8,
    HFHAD=9,
    SC=10,
    HO=11,
    HGC_ECAL=12,
    HGC_HCALF=13,
    HGC_HCALB=14,
    kNBETypes=15
} sub_det;

//---sub detectors cluster----------------------------------------------------------------
//---/ distance from reference element / reference to objetc /
typedef vector<pair<float, const reco::PFBlockElement*> > sub_det_elements;
//---CMSSW position-----------------------------------------------------------------------
typedef ROOT::Math::PositionVector3D<ROOT::Math::CylindricalEta3D<Double32_t> > REPPoint;
//---Cluster candidate map----------------------------------------------------------------
typedef map<const reco::PFCluster*, pair<const reco::PFCandidate*, float> > cluster_map;

//****************************************************************************************

float DeltaPhi (float phi1, float phi2)
{
    float delta_phi = TMath::Abs(phi1 - phi2);
    if (delta_phi > 2*TMath::Pi()) 
	delta_phi -= 2*TMath::Pi();
    if (delta_phi > TMath::Pi() && delta_phi < 2*TMath::Pi()) 
	delta_phi = 2*TMath::Pi() - delta_phi;
    return delta_phi;
}

//****************************************************************************************

float DeltaR (float eta1, float eta2, float phi1, float phi2)
{
    float d_phi = DeltaPhi(phi1, phi2);
    float d_eta = TMath::Abs(eta1 - eta2);

    return TMath::Sqrt(pow(d_eta,2)+pow(d_phi,2));
}

//****************************************************************************************

sub_det_elements GetLinks(reco::PFBlock* block, unsigned int iEle)
{
    sub_det_elements linkedElements;    
    //---insert the starting element---
    linkedElements.push_back(pair<float, const reco::PFBlockElement*>
                             (0, &block->elements()[iEle]));
    for(unsigned int jEle=0; jEle<block->elements().size(); jEle++)
    {
        //---compute distance between elements---
	unsigned int i=iEle;
	unsigned int j=jEle;
	if(j == i)
	    continue;
	if(i > j)				
	    std::swap(i, j);
	unsigned int index = j - i - 1;
	index += index*block->elements().size() - i*(i+1)/2;
        //---if linked add element to the list---
	if(block->linkData()[index].distance == -1)
	    continue;
	else 
	{
	    linkedElements.push_back(pair<float, const reco::PFBlockElement*>
                                     (block->linkData()[index].distance,
                                      &block->elements()[jEle]));
	}
    }
    return linkedElements;
}

//****************************************************************************************

int MatchPhotons(const reco::PFCandidate* photon, cluster_map* clusterList)
{
    reco::PFBlock block = *photon->elementsInBlocks().at(0).first.get();
    const reco::PFCluster* pfCluster = NULL;
    //---search for the right ECAL cluster---
    float min_dist_cluster=100;
    for(unsigned int iEle=0; iEle<block.elements().size(); iEle++)
    {
    	if(block.elements()[iEle].type() != 4)
    	    continue;
        sub_det_elements clusters = GetLinks(&block, iEle);        
        for(unsigned int iCls=0; iCls<clusters.size(); iCls++)
        {
            if(clusters.at(iCls).second->type() != 4)
                continue;
            reco::PFCluster tmpCluster = *clusters.at(iCls).second->clusterRef().get();
            tmpCluster.calculatePositionREP();
            REPPoint pfClusterPos = tmpCluster.positionREP();
            float tmp_dist=DeltaR(pfClusterPos.Eta(), photon->eta(),
                                  pfClusterPos.Phi(), photon->phi());
            if(tmp_dist < min_dist_cluster)
            {
                min_dist_cluster = tmp_dist;
                pfCluster = clusters.at(iCls).second->clusterRef().get();                
                cout << "DeltaR        : " << min_dist_cluster << endl
                     << "Cluster Energy: " << pfCluster->energy() << endl;
            }
        }        
    }
    if(pfCluster)
    {
        if(clusterList->find(pfCluster) == clusterList->end() ||
           clusterList->at(pfCluster).second > min_dist_cluster)
            (*clusterList)[pfCluster] = pair<const reco::PFCandidate*, float>(photon, min_dist_cluster);
        return 0;
    }
    return -1;
}

//****************************************************************************************

int MatchCharged(const reco::PFCandidate* charged, cluster_map* clusterList)
{
    reco::PFBlock block = *charged->elementsInBlocks().at(0).first.get();
    const reco::Track* pfTrack=NULL;
    const reco::PFCluster* pfCluster=NULL;
    //---search for the right ECAL cluster---
    float min_dist_cluster=100;
    for(unsigned int iEle=0; iEle<block.elements().size(); iEle++)
    {
        pfTrack = block.elements()[iEle].trackRef().get();
    	if(block.elements()[iEle].type() != 1 || charged->trackRef().get() != pfTrack)
    	    continue;
        sub_det_elements clusters = GetLinks(&block, iEle);        
        for(unsigned int iCls=0; iCls<clusters.size(); iCls++)
        {
            if(clusters.at(iCls).second->type() != 4)
                continue;
            reco::PFCluster tmpCluster = *clusters.at(iCls).second->clusterRef().get();
            tmpCluster.calculatePositionREP();
            REPPoint pfClusterPos = tmpCluster.positionREP();
            float tmp_dist=DeltaR(pfClusterPos.Eta(), charged->positionAtECALEntrance().Eta(),
                                  pfClusterPos.Phi(), charged->positionAtECALEntrance().Phi());
            if(tmp_dist < min_dist_cluster)
            {
                min_dist_cluster = tmp_dist;
                pfCluster = clusters.at(iCls).second->clusterRef().get();                
                cout << "DeltaR        : " << min_dist_cluster << endl
                     << "Cluster Energy: " << pfCluster->energy() << endl;
            }
        }
    }
    if(pfCluster)
    {
        if(clusterList->find(pfCluster) == clusterList->end() ||
           clusterList->at(pfCluster).second > min_dist_cluster)
            (*clusterList)[pfCluster] = pair<const reco::PFCandidate*, float>(charged, min_dist_cluster);
        return 0;
    }
    return -1;
}

pair<bool, float> GetTimeFromRecHits(const reco::PFCluster* pfCluster, vector<EcalRecHit>* recHitsEK)
{
    vector<pair<DetId,float> > detIdMap = pfCluster->hitsAndFractions();
    int nMatch=0;
    int hardestRecHit=0;
    float candTime=-1000;
    for(unsigned int iRec=0; iRec<recHitsEK->size(); iRec++)
    {
	for(unsigned int iDet=0; iDet<detIdMap.size(); iDet++)
	{
	    if(detIdMap.at(iDet).first == recHitsEK->at(iRec).id())
	    {
		nMatch++;
		if(recHitsEK->at(iRec).energy() >= recHitsEK->at(hardestRecHit).energy())
		{
		    candTime = recHitsEK->at(iRec).time();
		}
	    }
	}
    }
    if(nMatch > 0)
        return pair<bool, float>(true, candTime);
    else
        return pair<bool, float>(false, candTime);
    // cout << "PFCandidate ecal time   : " << candTime << endl
    //      << "# match/PFRec/totRec    : " << nMatch << "/" << detIdMap.size() << "/" << recHitsEK->size()
    //      << endl << endl;
}

//****************************************************************************************

int main(int argc, char* argv[])
{
    gSystem->Load("libFWCoreFWLite.so"); 
    AutoLibraryLoader::enable();

    //---histos---
    TFile* outFile = TFile::Open(argv[2], "recreate");    
    TH1F* h_neutral_time = new TH1F("neutral_time", "Time of the most energetic RecHit",
    				    200, -10, 10);
    TH1F* h_charged_time = new TH1F("charged_time", "Time of the most energetic RecHit",
    				    200, -10, 10);
    h_charged_time->SetFillColor(kRed);
    //---FWLite interfaces---
    TFile* inFile = TFile::Open(argv[1]);
    fwlite::Event event(inFile);
    fwlite::Handle<vector<reco::PFJet> > jetsHandle;
    fwlite::Handle<edm::SortedCollection<EcalRecHit, 
					 edm::StrictWeakOrdering<EcalRecHit > > > recSort;
    //---events loop---
    int iEvent=1;
    for(event.toBegin(); !event.atEnd(); ++event)
    {
	cout << "### EVENT: " << iEvent << endl;
	iEvent++;
	// if(iEvent != 48)
	//     continue;
	//candHandle.getByLabel(event, "particleFlow");
	//---get PFJets and hardest jet constituents---
	jetsHandle.getByLabel(event, "ak5PFJetsCHS");
	int goodJetIndex=0;
	for(unsigned int iJet=0; iJet<jetsHandle.ptr()->size(); iJet++)
	{
	    if(jetsHandle.ptr()->at(iJet).energy() !=
	       jetsHandle.ptr()->at(iJet).photonEnergy())
	    {
		goodJetIndex = iJet;
		break;
	    }
	}
	if(goodJetIndex == -1 || !jetsHandle.isValid())
	    continue;
	vector<edm::Ptr<reco::PFCandidate> > candidates =
	    jetsHandle.ptr()->at(goodJetIndex).getPFConstituents();
	// //---get EK detailed time RecHits---
	recSort.getByLabel(event, "ecalDetailedTimeRecHit", "EcalRecHitsEK", "RECO");
	if(!recSort.isValid())
	    continue;
	vector<EcalRecHit>* recVect = (vector<EcalRecHit>*)recSort.ptr();

	//---Particle container---
        vector<const reco::PFCandidate*> photons;
        vector<const reco::PFCandidate*> charged;
        cluster_map clusterList;
	//---Costituent loop---
	for(unsigned int iCand=0; iCand<candidates.size(); iCand++)
	{		
	    if(candidates.at(iCand).isNull())
		continue;
	    const reco::PFCandidate* pfCand = candidates.at(iCand).get();
	    if(!pfCand)
		cout << "ERROR: void pfCand" << endl;
	    if(pfCand->particleId() == 4)
                photons.push_back(pfCand);
	    else if (pfCand->particleId() < 4)
                charged.push_back(pfCand);
	}
        for(unsigned int iPho=0; iPho<photons.size(); iPho++)
            MatchPhotons(photons.at(iPho), &clusterList);
        for(unsigned int iChr=0; iChr<charged.size(); iChr++)
            MatchCharged(charged.at(iChr), &clusterList);
        for(cluster_map::iterator it=clusterList.begin(); it!=clusterList.end(); ++it)
        {
            pair<bool, float> particleTime = GetTimeFromRecHits(it->first, recVect);
            if(it->second.first->particleId() == 4 && particleTime.first)
                h_neutral_time->Fill(particleTime.second);
            if(it->second.first->particleId() < 4 && particleTime.first)
                h_charged_time->Fill(particleTime.second);
        }
    }
    outFile->cd();
    h_neutral_time->Write();
    h_charged_time->Write();
    outFile->Close();
    return 0;
}