// testing jetfinding algorithms
// on pythia
// Nick Elsey

// ROOT is used for histograms
// ROOT Headers
#include "TH1.h"
#include "TH2.h"
#include "TH3.h"
#include "TF1.h"
#include "TF2.h"
#include "TProfile.h"
#include "TProfile2D.h"
#include "TObjArray.h"
#include "TString.h"
#include "TFile.h"
#include "TLorentzVector.h"
#include "TClonesArray.h"
#include "TChain.h"
#include "TBranch.h"
#include "TMath.h"
#include "TRandom.h"
#include "TRandom3.h"
#include "TCanvas.h"
#include "TStopwatch.h"
#include "TSystem.h"

// My standard includes
// Make use of std::vector,
// std::string, IO and algorithm
// STL Headers
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include <random>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <thread>

// The analysis is run on FastJet::PseudoJets
// We make use of the jetfinding tools
// And the convenient FastJet::Selectors
#include "fastjet/PseudoJet.hh"
#include "fastjet/ClusterSequence.hh"
#include "fastjet/ClusterSequenceArea.hh"
#include "fastjet/ClusterSequencePassiveArea.hh"
#include "fastjet/ClusterSequenceActiveArea.hh"
#include "fastjet/ClusterSequenceActiveAreaExplicitGhosts.hh"
#include "fastjet/SISConePlugin.hh"
#include "fastjet/Selector.hh"
#include "fastjet/FunctionOfPseudoJet.hh"
#include "fastjet/tools/JetMedianBackgroundEstimator.hh"
#include "fastjet/tools/Subtractor.hh"
#include "fastjet/tools/Filter.hh"

// Pythia generator
#include "Pythia8/Pythia.h"

// the grid does not have std::to_string() for some ungodly reason
// replacing it here. Simply ostringstream
namespace patch {
  template < typename T > std::string to_string( const T& n )
  {
    std::ostringstream stm ;
    stm << n ;
    return stm.str() ;
  }
}

// used to convert pythia events to vectors of pseudojets
int convertToPseudoJet( Pythia8::Pythia& p, double max_rap, std::vector<fastjet::PseudoJet>& all, std::vector<fastjet::PseudoJet>& charged, std::vector<fastjet::PseudoJet>& part ) {
  
  // clear the event containers
  all.clear();
  charged.clear();
  part.clear();
  
  // get partons first
  // the initial protons are ids 1 & 2,
  // the hard scattering products are ids 5 & 6
  if (p.event[5].status() != -23)
    std::cerr<<"Error: assumption that id 5 is the outgoing parton is not valid."<<std::endl;
  if (p.event[6].status() != -23)
    std::cerr<<"Error: assumption that id 6 is the outgoing parton is not valid."<<std::endl;
  fastjet::PseudoJet part1( p.event[5].px(), p.event[5].py(), p.event[5].pz(), p.event[5].e() );
  part1.set_user_index( 3 * p.event[5].charge() );
  fastjet::PseudoJet part2( p.event[6].px(), p.event[6].py(), p.event[6].pz(), p.event[6].e() );
  part2.set_user_index( 3 * p.event[6].charge() );
  if ( fabs(part1.eta()) > max_rap || fabs(part2.eta()) >max_rap )
    return 0;
  part.push_back( part1 );
  part.push_back( part2 );
  
  // now loop over all particles, and fill the vectors
  for ( int i = 0; i < p.event.size(); ++i ) {
    if ( p.event[i].isFinal() && p.event[i].isVisible() ) {
      fastjet::PseudoJet tmp( p.event[i].px(), p.event[i].py(), p.event[i].pz(), p.event[i].e() );
      tmp.set_user_index( p.event[i].charge() );
      
      // check to make sure its in our rapidity range
      if ( fabs( tmp.rap() ) > max_rap  )
        continue;
      
      all.push_back( tmp );
      if ( p.event[i].charge() )
        charged.push_back( tmp );
      
    }
  }
  
  return 1;
}

// Arguments
// 0: xml directory for pythia
// 1: exponent base 10 for number of events
// 2: output location


int main( int argc, const char** argv ) {
  
  // Histograms will calculate gaussian errors
  // -----------------------------------------
  TH1::SetDefaultSumw2( );
  TH2::SetDefaultSumw2( );
  TH3::SetDefaultSumw2( );
  
  typedef std::chrono::high_resolution_clock clock;
  
  // we will time the analysis
  std::chrono::time_point<clock> analysis_start = clock::now();
  
  // set parameters
  unsigned exponent;
  std::string outFile;
  std::string xmldir;

  switch ( argc ) {
    case 1: {
      exponent = 1;
      outFile = "out/test.root";
      //xmldir = "/Users/nick/physics/software/pythia8/share/Pythia8/xmldoc";
      xmldir = "/wsu/home/dx/dx54/dx5412/software/pythia8219/share/Pythia8/xmldoc";
      break;
    }
    case 4: {
      xmldir = argv[1];
      exponent = atoi( argv[2] );
      outFile = argv[3];
      break;
    }
    default: {
      std::cerr<<"Error: unexpected number of inputs."<<std::endl;
      return -1;
    }
  }
  
  // set the total number of events as
  // 10^exponent
  unsigned maxEvent = pow( 10, exponent );
  std::cout<<"set for "<<maxEvent<<" events"<<std::endl;
  
  // setup pythia
  // ------------
  
  // create the pythia generator and initialize it with the xmldoc in my pythia8 directory
  //Pythia8::Pythia pythia( xmldir );
  Pythia8::Pythia pythia;
  
  // settings for LHC pp at 13 TeV
  pythia.readString("Beams:eCM = 13000");
  pythia.readString("HardQCD:all = on");
  pythia.readString("Random:setSeed = on");
  pythia.readString("Random:seed = 0");
  pythia.readString("PhaseSpace:pTHatMin = 200.0");
  
  // initialize the pythia generator
  pythia.init();
  pythia.next();
  // set jet finding parameters
  // --------------------------

  // set a hard cut on rapidity for all tracks
  const double max_track_rap = 4.0;
  const double max_rap = max_track_rap;
  
  // first some base jetfinding definitions
  double baseRadius = 0.8;
  fastjet::JetDefinition antiKtBase( fastjet::antikt_algorithm, baseRadius );
  fastjet::JetDefinition KtBase( fastjet::kt_algorithm, baseRadius );
  fastjet::JetDefinition CaBase( fastjet::cambridge_algorithm, baseRadius );

  // but we will also be testing these with different radii, so we'll initialize that here
  // there will be nRadii different radii, in increments of deltaRad;
  const int nRadii = 10;
  double deltaRad = 0.1;
  double overlap_threshold = 0.75;
  double radii[nRadii];
  fastjet::JetDefinition antiKtDefs[nRadii];
  fastjet::JetDefinition KtDefs[nRadii];
  fastjet::JetDefinition CaDefs[nRadii];
  fastjet::JetDefinition SISDefs[nRadii];
  
  for ( int i = 0; i < nRadii; ++i ) {
    radii[i] = deltaRad * (i+1);

    antiKtDefs[i] = fastjet::JetDefinition( fastjet::antikt_algorithm, radii[i] );
    KtDefs[i] = fastjet::JetDefinition( fastjet::kt_algorithm, radii[i] );
    CaDefs[i] = fastjet::JetDefinition( fastjet::cambridge_algorithm, radii[i] );

    // and for SISCone
    fastjet::JetDefinition::Plugin * plugin = new fastjet::SISConePlugin(radii[i], overlap_threshold);
    SISDefs[i] = fastjet::JetDefinition(plugin);
  }

  // set up our fastjet environment
  // ------------------------------
  
  // We'll be using fastjet:PseudoJet for all our work
  // so we'll make a few helpers
  
  // this will include all final state particles
  // ( minus neutrinos )
  std::vector<fastjet::PseudoJet> allFinal;
  
  // this will include all charged particles in the final state
  std::vector<fastjet::PseudoJet> chargedFinal;
  
  // this will be the two partons from the scattering
  std::vector<fastjet::PseudoJet> partons;
  
  
  
  // create an area definition for the clustering
  //----------------------------------------------------------
  // ghosts should go up to the acceptance of the detector or
  // (with infinite acceptance) at least 2R beyond the region
  // where you plan to investigate jets.
  const int ghost_repeat = 1;
  const double ghost_area = 0.01;
  // we'll set ghost_max_rap to the largest applicable based
  // on our radius settings
  const double ghost_max_rap = max_rap + 2.0 * radii[nRadii];
  
  fastjet::GhostedAreaSpec area_spec = fastjet::GhostedAreaSpec( ghost_max_rap, ghost_repeat, ghost_area );
  fastjet::AreaDefinition  area_def = fastjet::AreaDefinition(fastjet::active_area_explicit_ghosts, area_spec);
  
  
  // create output histograms using root
  TH1D* multiplicity = new TH1D("mult", "Visible Multiplicity", 300, -0.5, 899.5 );
  TH1D* chargedMultiplicity = new TH1D("chargemult", "Charged Multiplicity", 300, -0.5, 899.5 );
  TH1D* partonPt = new TH1D("partonpt", "Parton Pt", 100, 0, 1000 );
  TH1D* partonE = new TH1D( "parton_e", "Parton Energy", 100, 0, 1000 );
  TH2D* partonEtaPhi = new TH2D("partonetaphi", "Parton Eta x Phi", 100, -5, 5, 100, -TMath::Pi(), TMath::Pi() );
  
  // associated particle information
  TH1D* visiblePt = new TH1D( "finalstatept", "Detected Pt", 200, 0, 100 );
  TH1D* visibleE = new TH1D( "finalstateE", "Detected E", 200, 0, 100 );
  TH2D* visibleEtaPhi = new TH2D( "finaletaphi", "Detected Eta x Phi",  100, -5, 5, 100, -TMath::Pi(), TMath::Pi() );
  TH1D* chargedPt = new TH1D("chargedfstatept", "Detected Charged Pt", 200, 0, 100);
  TH1D* chargedE = new TH1D( "chargedfstateE", "Detected Charged E", 200, 0, 100 );
  TH2D* chargedEtaPhi = new TH2D( "chargedetaphi", "Detected Charged Eta x Phi",  100, -12, 12, 100, -TMath::Pi(), TMath::Pi() );
  
  // jet information
  TH1D* nJetsAntiKtBaseAll = new TH1D("njetsantiktbase", "Jet Multiplicity Anti-Kt Base", 50, 49.5, 299.5);
  TH1D* nJetsKtBaseAll = new TH1D("njetsktbase", "Jet Multiplicity Kt Base", 50, 49.5, 299.5);
  TH1D* nJetsCaBaseAll = new TH1D("njetsCabase", "Jet Multiplicity CA Base", 50, 49.5, 299.5);
  TH1D* nJetsAntiKtBaseCharged = new TH1D("njetsantiktbasecharged", "Jet Multiplicity Anti-Kt Base Charged", 50, 49.5, 299.5);
  TH1D* nJetsKtBaseCharged = new TH1D("njetsktbasecharged", "Jet Multiplicity Kt Base Charged", 50, 49.5, 299.5);
  TH1D* nJetsCaBaseCharged = new TH1D("njetsCabasecharged", "Jet Multiplicity CA Base Charged", 50, 49.5, 299.5);
  
  
  
  // make a histogram for all of the differing radii
  
  // antikt
  TH2D* nJetsAntiKt = new TH2D( "antiktnjets", "Number of Jets - Anti-Kt", nRadii, -0.5, nRadii-0.5, 300, -0.5, 599.5 );
  TH2D* deltaEAntiKt = new TH2D( "antiktdeltaE", "#Delta E - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, -100, 100 );
  TH2D* deltaRAntiKt = new TH2D( "antiktdeltaR", "#Delta R Leading - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2.0 );
  TH2D* nPartAntiKt = new TH2D( "antiktnpart", "Number of Particles per Jet - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* nPartLeadAntiKt = new TH2D( "antiktnpartlead", "Number of Particles per Leading Jet - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* timeAntiKt = new TH2D("antiktclustertime", "Time Required to cluster - Anti-Kt", nRadii, -0.5, nRadii-0.5, 500, 0, 20);
  TH2D* areaAntiKt = new TH2D("antiktarea", "Jet Area - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* areaLeadAntiKt = new TH2D("antiktarealead", "Lead Jet Area - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* ptLeadAntiKt = new TH2D("antiktptlead", "Lead Jet Pt - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* eLeadAntiKt = new TH2D("antiktelead", "Lead Jet Energy - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* etaAntiKt = new TH2D("antikteta", "Jet Eta - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap );
  TH2D* etaLeadAntiKt = new TH2D("antiktetalead", "Lead Jet Eta - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap);
  TH2D* phiAntiKt = new TH2D("antiktphi", "Jet Phi - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );
  TH2D* phiLeadAntiKt = new TH2D("antiktphilead", "Lead Jet Phi - Anti-Kt", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );
  
  // kt
  TH2D* nJetsKt = new TH2D( "ktnjets", "Number of Jets - Kt", nRadii, -0.5, nRadii-0.5, 300, -0.5, 599.5 );;
  TH2D* deltaEKt = new TH2D( "ktdeltaE", "#Delta E - Kt", nRadii, -0.5, nRadii-0.5, 100, -100, 100 );
  TH2D* deltaRKt = new TH2D( "ktdeltaR", "#Delta R - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2.0 );
  TH2D* nPartKt = new TH2D( "ktnpart", "Number of Particles per Jet - Kt", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* nPartLeadKt = new TH2D( "ktnpartlead", "Number of Particles per Leading Jet - Kt", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* timeKt = new TH2D("ktclustertime", "Time Required to cluster - Kt", nRadii, -0.5, nRadii-0.5, 500, 0, 20);
  TH2D* areaKt = new TH2D("ktarea", "Jet Area - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* areaLeadKt = new TH2D("ktarealead", "Lead Jet Area - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* ptLeadKt = new TH2D("ktptlead", "Lead Jet Pt - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* eLeadKt = new TH2D("ktelead", "Lead Jet Energy - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* etaKt = new TH2D("kteta", "Jet Eta - Kt", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap );
  TH2D* etaLeadKt = new TH2D("ktetalead", "Lead Jet Eta - Kt", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap);
  TH2D* phiKt = new TH2D("ktphi", "Jet Phi - Kt", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );
  TH2D* phiLeadKt = new TH2D("ktphilead", "Lead Jet Phi - Kt", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );

  // cambridge
  TH2D* nJetsCa = new TH2D( "canjets", "Number of Jets - CA", nRadii, -0.5, nRadii-0.5, 300, -0.5, 599.5 );
  TH2D* deltaECa = new TH2D( "cadeltaE", "#Delta E - CA", nRadii, -0.5, nRadii-0.5, 100, -100, 100 );
  TH2D* deltaRCa = new TH2D( "cadeltaR", "#Delta R Leading - CA", nRadii, -0.5, nRadii-0.5, 100, 0, 2.0 );
  TH2D* nPartCa = new TH2D( "canpart", "Number of Particles per Jet - CA", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* nPartLeadCa = new TH2D( "canpartlead", "Number of Particles per Leading Jet - CA", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* timeCa = new TH2D("caclustertime", "Time Required to cluster - CA", nRadii, -0.5, nRadii-0.5, 500, 0, 20);
  TH2D* areaCa = new TH2D("caarea", "Jet Area - CA", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* areaLeadCa = new TH2D("caarealead", "Lead Jet Area - CA", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* ptLeadCa = new TH2D("captlead", "Lead Jet Pt - CA", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* eLeadCa = new TH2D("caelead", "Lead Jet Energy - CA", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* etaCa = new TH2D("caeta", "Jet Eta - CA", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap );
  TH2D* etaLeadCa = new TH2D("caetalead", "Lead Jet Eta - CA", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap);
  TH2D* phiCa = new TH2D("caphi", "Jet Phi - CA", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );
  TH2D* phiLeadCa = new TH2D("caphilead", "Lead Jet Phi - CA", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );
  
  // siscone
  TH2D* nJetsSIS = new TH2D( "sisnjets", "Number of Jets - Kt", nRadii, -0.5, nRadii-0.5, 300, -0.5, 599.5 );;
  TH2D* deltaESIS = new TH2D( "sisdeltaE", "#Delta E - Kt", nRadii, -0.5, nRadii-0.5, 100, -100, 100 );
  TH2D* deltaRSIS = new TH2D( "sisdeltaR", "#Delta R - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2.0 );
  TH2D* nPartSIS = new TH2D( "sisnpart", "Number of Particles per Jet - Kt", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* nPartLeadSIS = new TH2D( "sisnpartlead", "Number of Particles per Leading Jet - Kt", nRadii, -0.5, nRadii-0.5, 100, -0.5, 599.5 );
  TH2D* timeSIS = new TH2D("sisclustertime", "Time Required to cluster - Kt", nRadii, -0.5, nRadii-0.5, 500, 0, 20000);
  TH2D* areaSIS = new TH2D("sisarea", "Jet Area - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* areaLeadSIS = new TH2D("sisarealead", "Lead Jet Area - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 2*TMath::Pi() );
  TH2D* ptLeadSIS = new TH2D("sisptlead", "Lead Jet Pt - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* eLeadSIS = new TH2D("siselead", "Lead Jet Energy - Kt", nRadii, -0.5, nRadii-0.5, 100, 0, 1000 );
  TH2D* etaSIS = new TH2D("siseta", "Jet Eta - Kt", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap );
  TH2D* etaLeadSIS = new TH2D("sisetalead", "Lead Jet Eta - Kt", nRadii, -0.5, nRadii-0.5, 100, -max_rap, max_rap);
  TH2D* phiSIS = new TH2D("sisphi", "Jet Phi - Kt", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );
  TH2D* phiLeadSIS = new TH2D("sisphilead", "Lead Jet Phi - Kt", nRadii, -0.5, nRadii-0.5, 100, -TMath::Pi(), TMath::Pi() );
  // set bin labels to radii
  for ( int i = 1; i <= nRadii; ++i ) {

    nJetsAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaEAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaRAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartLeadAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    timeAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    areaAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    areaLeadAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    ptLeadAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    eLeadAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaLeadAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiLeadAntiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );

    nJetsKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaEKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaRKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartLeadKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    timeKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    areaKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    areaLeadKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    ptLeadKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    eLeadKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaLeadKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiLeadKt->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );

    nJetsCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaECa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaRCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartLeadCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    timeCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    areaCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    areaLeadCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    ptLeadCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    eLeadCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaLeadCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiLeadCa->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );

    nJetsSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaESIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    deltaRSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    nPartLeadSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    timeSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    areaSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    areaLeadSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1] ).c_str() );
    ptLeadSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    eLeadSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    etaLeadSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );
    phiLeadSIS->GetXaxis()->SetBinLabel( i, patch::to_string( radii[i-1]).c_str() );

  }
  
  // start the event loop from event 0
  unsigned currentEvent = 0;
  try{
    while ( currentEvent < maxEvent ) {
      // try to generate a new event
      // if it fails, iterate without incrementing
      // current event number
      
      if ( !pythia.next() )
        continue;
      
      // pythia succeeded, so increment the event
      currentEvent++;

      // output event number
      if ( currentEvent%50 == 0 )
        std::cout<<"Event: "<<currentEvent<<std::endl;
      
      // convert pythia particles into useable pseudojets,
      // only take those in our eta range && that are visible
      // in conventional detectors
      // note: particles user_index() is the charge
      // if partons are outside
      convertToPseudoJet( pythia, max_track_rap, allFinal, chargedFinal, partons );

      // event information
      multiplicity->Fill( allFinal.size() );
      chargedMultiplicity->Fill( chargedFinal.size() );
      
      // fill parton information
      for ( int i = 0; i < 2; ++i ) {
        partonEtaPhi->Fill( partons[i].eta(), partons[i].phi_std() );
        partonPt->Fill( partons[i].pt() );
        partonE->Fill( partons[i].E() );
      }
      
      // now fill track information
      for ( int i = 0; i < allFinal.size(); ++i ) {
        visiblePt->Fill( allFinal[i].pt() );
        visibleE->Fill( allFinal[i].E() );
        visibleEtaPhi->Fill( allFinal[i].eta(), allFinal[i].phi_std() );
      }
      
      for ( int i = 0; i < chargedFinal.size(); ++i ) {
        chargedPt->Fill( chargedFinal[i].pt() );
        chargedE->Fill( chargedFinal[i].E() );
        chargedEtaPhi->Fill( chargedFinal[i].eta(), chargedFinal[i].phi_std() );
      }
      
      // // now set up the clustering
      // fastjet::ClusterSequenceArea clusterAntiKtAll ( allFinal, antiKtBase, area_def );
      // std::vector<fastjet::PseudoJet> antiKtBaseJets = fastjet::sorted_by_pt(clusterAntiKtAll.inclusive_jets());
      // fastjet::ClusterSequenceArea clusterKtAll ( allFinal, KtBase, area_def );
      // std::vector<fastjet::PseudoJet> KtBaseJets = fastjet::sorted_by_pt(clusterKtAll.inclusive_jets());
      // fastjet::ClusterSequenceArea clusterCaAll ( allFinal, CaBase, area_def );
      // std::vector<fastjet::PseudoJet> CaBaseJets = fastjet::sorted_by_pt(clusterCaAll.inclusive_jets());
      // fastjet::ClusterSequenceArea clusterAntiKtCharged ( chargedFinal, antiKtBase, area_def );
      // std::vector<fastjet::PseudoJet> antiKtChargedJets = fastjet::sorted_by_pt(clusterAntiKtCharged.inclusive_jets());
      // fastjet::ClusterSequenceArea clusterKtCharged ( chargedFinal, KtBase, area_def );
      // std::vector<fastjet::PseudoJet> KtChargedJets = fastjet::sorted_by_pt(clusterKtCharged.inclusive_jets());
      // fastjet::ClusterSequenceArea clusterCaCharged ( chargedFinal, CaBase, area_def );
      // std::vector<fastjet::PseudoJet> CaChargedJets = fastjet::sorted_by_pt(clusterCaCharged.inclusive_jets());

      // // plot the number of jets by the different algorithms
      // nJetsAntiKtBaseAll->Fill( antiKtBaseJets.size() );
      // nJetsKtBaseAll->Fill( KtBaseJets.size() );
      // nJetsCaBaseAll->Fill( CaBaseJets.size() );
      // nJetsAntiKtBaseCharged->Fill( antiKtChargedJets.size() );
      // nJetsKtBaseCharged->Fill( KtChargedJets.size() );
      // nJetsCaBaseCharged->Fill( CaChargedJets.size() );

      // now we'll do the loop over differing radii
      for ( int i = 0; i < nRadii; ++i ) {
        
        
        std::string radBin = patch::to_string( radii[i] );
        
        // first perform the clustering
        
        // time the clustering as well
        std::chrono::time_point<clock> start = clock::now();
        
        fastjet::ClusterSequenceArea clusterAntiKt( allFinal, antiKtDefs[i], area_def );
        //fastjet::ClusterSequence clusterAntiKt( allFinal, antiKtDefs[i] );
        double antiKtTime = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
        start = clock::now();
        
        fastjet::ClusterSequenceArea clusterKt( allFinal, KtDefs[i], area_def );
        //fastjet::ClusterSequence clusterKt( allFinal, KtDefs[i] );
        double ktTime = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
        start = clock::now();
        
        fastjet::ClusterSequenceArea clusterCa( allFinal, CaDefs[i], area_def );
        //fastjet::ClusterSequence clusterCa( allFinal, CaDefs[i] );
        double caTime = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
        start = clock::now();

        fastjet::ClusterSequenceArea clusterSIS( allFinal, SISDefs[i], area_def );
        double SISTime = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
        
        // fill timing measurements
        timeAntiKt->Fill( radBin.c_str(), antiKtTime, 1 );
        timeKt->Fill( radBin.c_str(), ktTime, 1 );
        timeCa->Fill( radBin.c_str(), caTime, 1 );
        timeSIS->Fill( radBin.c_str(), SISTime, 1 );
        
        std::vector<fastjet::PseudoJet> antiKtJets = fastjet::sorted_by_pt( fastjet::SelectorPtMin(1.0)(clusterAntiKt.inclusive_jets()) );
        std::vector<fastjet::PseudoJet> KtJets = fastjet::sorted_by_pt( fastjet::SelectorPtMin(1.0)(clusterKt.inclusive_jets()) );
        std::vector<fastjet::PseudoJet> CaJets = fastjet::sorted_by_pt( fastjet::SelectorPtMin(1.0)(clusterCa.inclusive_jets()) );
        std::vector<fastjet::PseudoJet> SISJets = fastjet::sorted_by_pt( fastjet::SelectorPtMin(1.0)(clusterSIS.inclusive_jets()) );
        // now start to fill histograms
        // first, number of jets in the event
        nJetsAntiKt->Fill ( radBin.c_str(), antiKtJets.size(), 1 );
        nJetsKt->Fill ( radBin.c_str(), KtJets.size(), 1 );
        nJetsCa->Fill ( radBin.c_str(), CaJets.size(), 1 );
        nJetsSIS->Fill( radBin.c_str(), SISJets.size(), 1 );
        
        // now, we'll do number of particles, and area, for both both leading jets and inclusive jets
        nPartLeadAntiKt->Fill ( radBin.c_str(), antiKtJets[0].constituents().size(), 1 );
        nPartLeadKt->Fill ( radBin.c_str(), KtJets[0].constituents().size(), 1 );
        nPartLeadCa->Fill ( radBin.c_str(), CaJets[0].constituents().size(), 1 );
        nPartLeadSIS->Fill ( radBin.c_str(), SISJets[0].constituents().size(), 1 );
        areaLeadAntiKt->Fill ( radBin.c_str(), antiKtJets[0].area(), 1 );
        areaLeadKt->Fill ( radBin.c_str(), KtJets[0].area(), 1 );
        areaLeadCa->Fill ( radBin.c_str(), CaJets[0].area(), 1 );
        areaLeadSIS->Fill( radBin.c_str(), SISJets[0].area(), 1 );

        // fill leading jet spectra
        ptLeadAntiKt->Fill( radBin.c_str(), antiKtJets[0].pt(), 1 );
        eLeadAntiKt->Fill( radBin.c_str(), antiKtJets[0].E(), 1 );
        ptLeadKt->Fill( radBin.c_str(), KtJets[0].pt(), 1 );
        eLeadKt->Fill( radBin.c_str(), KtJets[0].E(), 1 );
        ptLeadCa->Fill( radBin.c_str(), CaJets[0].pt(), 1 );
        eLeadCa->Fill( radBin.c_str(), CaJets[0].E(), 1 );
        ptLeadSIS->Fill( radBin.c_str(), SISJets[0].pt(), 1 );
        eLeadSIS->Fill( radBin.c_str(), SISJets[0].E(), 1 );
        
        // leading jet eta & phi
        etaLeadAntiKt->Fill( radBin.c_str(), antiKtJets[0].eta(), 1 );
        phiLeadAntiKt->Fill( radBin.c_str(), antiKtJets[0].phi_std(), 1 );
        etaLeadKt->Fill( radBin.c_str(), KtJets[0].eta(), 1 );
        phiLeadKt->Fill( radBin.c_str(), KtJets[0].phi_std(), 1 );
        etaLeadCa->Fill( radBin.c_str(), CaJets[0].eta(), 1 );
        phiLeadCa->Fill( radBin.c_str(), CaJets[0].phi_std(), 1 );
        etaLeadSIS->Fill( radBin.c_str(), SISJets[0].eta(), 1 );
        phiLeadSIS->Fill( radBin.c_str(), SISJets[0].phi_std(), 1 );
        
        for ( int j = 0; j < antiKtJets.size(); ++j ) {
          nPartAntiKt->Fill ( radBin.c_str(), antiKtJets[j].constituents().size(), 1 );
          areaAntiKt->Fill ( radBin.c_str(), antiKtJets[j].area(), 1 );
          etaAntiKt->Fill( radBin.c_str(), antiKtJets[j].eta(), 1 );
          phiAntiKt->Fill( radBin.c_str(), antiKtJets[j].phi_std(), 1 );
        }
        for ( int j = 0; j < KtJets.size(); ++j ) {
          nPartKt->Fill ( radBin.c_str(), KtJets[j].constituents().size(), 1 );
          areaKt->Fill ( radBin.c_str(), KtJets[j].area(), 1 );
          etaKt->Fill( radBin.c_str(), KtJets[j].eta(), 1 );
          phiKt->Fill( radBin.c_str(), KtJets[j].phi_std(), 1 );
        }
        for ( int j = 0; j < CaJets.size(); ++j ) {
          nPartCa->Fill ( radBin.c_str(), CaJets[j].constituents().size(), 1 );
          areaCa->Fill ( radBin.c_str(), CaJets[j].area(), 1 );
          etaCa->Fill( radBin.c_str(), CaJets[j].eta(), 1 );
          phiCa->Fill( radBin.c_str(), CaJets[j].phi_std(), 1 );
        }
        for ( int j = 0; j < SISJets.size(); ++j ) {
          nPartSIS->Fill( radBin.c_str(), SISJets[j].constituents().size(), 1 );
          areaSIS->Fill( radBin.c_str(), SISJets[j].area(), 1 );
          etaSIS->Fill( radBin.c_str(), SISJets[j].eta(), 1 );
          phiSIS->Fill( radBin.c_str(), SISJets[j].phi_std(), 1 );
        }
        
        // and compare to the initial partons for delta E and delta R
        // we find the minimum of the delta R between leading jet and parton1 and parton2
        // and use that as the base for both delta R and delta E
        
        // first antikt
        double distToPart1 = partons[0].delta_R(antiKtJets[0]);
        double distToPart2 = partons[1].delta_R(antiKtJets[0]);
        int partonIdx = 0;
        if ( distToPart2 < distToPart1 )
          partonIdx = 1;
        deltaRAntiKt->Fill ( radBin.c_str(), partons[partonIdx].delta_R( antiKtJets[0] ), 1 );
        deltaEAntiKt->Fill ( radBin.c_str(), partons[partonIdx].E() - antiKtJets[0].E(), 1 );
        
        // repeat for Kt, Ca and SIScone
        distToPart1 = partons[0].delta_R( KtJets[0] );
        distToPart2 = partons[1].delta_R( KtJets[0] );
        partonIdx = 0;
        if ( distToPart2 < distToPart1 )
          partonIdx = 1;
        deltaRKt->Fill ( radBin.c_str(), partons[partonIdx].delta_R( KtJets[0] ), 1 );
        deltaEKt->Fill ( radBin.c_str(), partons[partonIdx].E() - KtJets[0].E(), 1 );
        
        distToPart1 = partons[0].delta_R( CaJets[0] );
        distToPart2 = partons[1].delta_R( CaJets[0] );
        partonIdx = 0;
        if ( distToPart2 < distToPart1 )
          partonIdx = 1;
        deltaRCa->Fill ( radBin.c_str(), partons[partonIdx].delta_R( CaJets[0] ), 1 );
        deltaECa->Fill ( radBin.c_str(), partons[partonIdx].E() - CaJets[0].E(), 1 );
        
        distToPart1 = partons[0].delta_R( SISJets[0] );
        distToPart2 = partons[1].delta_R( SISJets[0] );
        partonIdx = 0;
        if ( distToPart2 < distToPart1 )
          partonIdx = 1;
        deltaRSIS->Fill ( radBin.c_str(), partons[partonIdx].delta_R( SISJets[0] ), 1 );
        deltaESIS->Fill ( radBin.c_str(), partons[partonIdx].E() - SISJets[0].E(), 1 );
      }
      
    }
  } catch ( std::exception& e) {
    std::cerr << "Caught " << e.what() << std::endl;
    return -1;
  }
  std::cout<<"processed "<<currentEvent<<" events"<<std::endl;
  
  // print out pythia statistics
  pythia.stat();
  
  // write out to a root file all histograms
  TFile out( outFile.c_str(), "RECREATE" );
  
  // event information
  multiplicity->Write();
  chargedMultiplicity->Write();
  
  // parton information
  partonEtaPhi->Write();
  partonPt->Write();
  partonE->Write();
  
  // track information
  visiblePt->Write();
  visibleE->Write();
  visibleEtaPhi->Write();
  chargedPt->Write();
  chargedE->Write();
  chargedEtaPhi->Write();
  
  // // jet multiplicity
  // nJetsAntiKtBaseAll->Write();
  // nJetsKtBaseAll->Write();
  // nJetsCaBaseAll->Write();
  // nJetsAntiKtBaseCharged->Write();
  // nJetsKtBaseCharged->Write();
  // nJetsCaBaseCharged->Write();
  
  // histograms for differing radii
  nJetsAntiKt->Write();
  nPartAntiKt->Write();
  nPartLeadAntiKt->Write();
  deltaEAntiKt->Write();
  deltaRAntiKt->Write();
  timeAntiKt->Write();
  areaAntiKt->Write();
  areaLeadAntiKt->Write();
  ptLeadAntiKt->Write();
  eLeadAntiKt->Write();
  etaAntiKt->Write();
  etaLeadAntiKt->Write();
  phiAntiKt->Write();
  phiLeadAntiKt->Write();
  
  nJetsKt->Write();
  nPartKt->Write();
  nPartLeadKt->Write();
  deltaEKt->Write();
  deltaRKt->Write();
  timeKt->Write();
  areaKt->Write();
  areaLeadKt->Write();
  ptLeadKt->Write();
  eLeadKt->Write();
  etaKt->Write();
  etaLeadKt->Write();
  phiKt->Write();
  phiLeadKt->Write();
  
  nJetsCa->Write();
  nPartCa->Write();
  nPartLeadCa->Write();
  deltaECa->Write();
  deltaRCa->Write();
  timeCa->Write();
  areaCa->Write();
  areaLeadCa->Write();
  ptLeadCa->Write();
  eLeadCa->Write();
  etaCa->Write();
  etaLeadCa->Write();
  phiCa->Write();
  phiLeadCa->Write();

  nJetsSIS->Write();
  nPartSIS->Write();
  nPartLeadSIS->Write();
  deltaESIS->Write();
  deltaRSIS->Write();
  timeSIS->Write();
  areaSIS->Write();
  areaLeadSIS->Write();
  ptLeadSIS->Write();
  eLeadSIS->Write();
  etaSIS->Write();
  etaLeadSIS->Write();
  phiSIS->Write();
  phiLeadSIS->Write();
  
  // close the output file
  out.Close();
  
  // stop timing and report
  double analysis_time = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - analysis_start).count();
  std::cout<<"Analysis of " << maxEvent <<" Pythia events took " << analysis_time << " seconds. Exiting" << std::endl;
  
  return 0;
}



