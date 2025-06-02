#include "ns3/core-module.h"
#include "../model/network-coding-encoder.h"
#include "../model/network-coding-packet.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include "../model/galois-field.h"
#include <iomanip>

using namespace ns3;

// Add this function BEFORE main()
void TestGaloisField()
{
  std::cout << "=== GALOIS FIELD BASIC OPERATIONS TEST ===" << std::endl;
  
  // Create a Galois Field instance
  Ptr<GaloisField> gf = CreateObject<GaloisField>();
  
  // Test 1: Basic operations with known values
  std::cout << "Test 1: Basic GF Operations" << std::endl;
  
  uint8_t a = 5;
  uint8_t b = 10;
  
  // Addition (XOR in GF(2^8))
  uint8_t sum = gf->Add(a, b);
  std::cout << "  5 + 10 = " << (int)sum << " (expected: 15)" << std::endl;
  
  // Multiplication 
  uint8_t product = gf->Multiply(a, b);
  std::cout << "  5 * 10 = " << (int)product << std::endl;
  
  // Check distributive property: a*(b+c) = a*b + a*c
  uint8_t c = 20;
  uint8_t sum_bc = gf->Add(b, c);
  uint8_t left = gf->Multiply(a, sum_bc);
  
  uint8_t prod_ab = gf->Multiply(a, b);
  uint8_t prod_ac = gf->Multiply(a, c);
  uint8_t right = gf->Add(prod_ab, prod_ac);
  
  std::cout << "  Distributive property: " 
            << (left == right ? "PASSED" : "FAILED") << std::endl;
  std::cout << "    a*(b+c) = " << (int)left << std::endl;
  std::cout << "    a*b + a*c = " << (int)right << std::endl;
  
  // Test 2: Linear combinations with simple values
  std::cout << "\nTest 2: Linear Combinations (Packet Encoding)" << std::endl;
  
  // Create simple "packets" - Just arrays of bytes
  std::vector<uint8_t> packet1 = {1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<uint8_t> packet2 = {8, 7, 6, 5, 4, 3, 2, 1};
  
  std::cout << "  Packet1: ";
  for (size_t i = 0; i < packet1.size(); i++) {
    std::cout << (int)packet1[i] << " ";
  }
  std::cout << std::endl;
  
  std::cout << "  Packet2: ";
  for (size_t i = 0; i < packet2.size(); i++) {
    std::cout << (int)packet2[i] << " ";
  }
  std::cout << std::endl;
  
  // Test 2.1: Create linear combination with coefficients [1, 0]
  // This should just be packet1
  std::vector<uint8_t> coeffs1 = {1, 0};
  std::vector<uint8_t> result1(packet1.size(), 0);
  
  for (size_t i = 0; i < packet1.size(); i++) {
    result1[i] = gf->Add(
      gf->Multiply(coeffs1[0], packet1[i]), 
      gf->Multiply(coeffs1[1], packet2[i])
    );
  }
  
  std::cout << "  Linear Combination [1, 0]: ";
  for (size_t i = 0; i < result1.size(); i++) {
    std::cout << (int)result1[i] << " ";
  }
  std::cout << std::endl;
  
  // Test 2.2: Create linear combination with coefficients [0, 1]
  // This should just be packet2
  std::vector<uint8_t> coeffs2 = {0, 1};
  std::vector<uint8_t> result2(packet1.size(), 0);
  
  for (size_t i = 0; i < packet1.size(); i++) {
    result2[i] = gf->Add(
      gf->Multiply(coeffs2[0], packet1[i]), 
      gf->Multiply(coeffs2[1], packet2[i])
    );
  }
  
  std::cout << "  Linear Combination [0, 1]: ";
  for (size_t i = 0; i < result2.size(); i++) {
    std::cout << (int)result2[i] << " ";
  }
  std::cout << std::endl;
  
  // Test 3: Different random coefficients should produce different results
  std::cout << "\nTest 3: Random Coefficients" << std::endl;
  
  // Set up different coefficient sets
  std::vector<std::vector<uint8_t>> coeffSets = {
    {2, 3},   // Set A
    {5, 7},   // Set B
    {11, 13}  // Set C
  };
  
  std::vector<std::vector<uint8_t>> results;
  
  for (size_t set = 0; set < coeffSets.size(); set++) {
    std::vector<uint8_t>& coeffs = coeffSets[set];
    std::vector<uint8_t> result(packet1.size(), 0);
    
    for (size_t i = 0; i < packet1.size(); i++) {
      result[i] = gf->Add(
        gf->Multiply(coeffs[0], packet1[i]), 
        gf->Multiply(coeffs[1], packet2[i])
      );
    }
    
    results.push_back(result);
    
    std::cout << "  Linear Combination [" << (int)coeffs[0] << ", " 
              << (int)coeffs[1] << "]: ";
    for (size_t i = 0; i < result.size(); i++) {
      std::cout << (int)result[i] << " ";
    }
    std::cout << std::endl;
  }
  
  // Check if all results are different (they should be with different coefficients)
  bool allDifferent = true;
  for (size_t i = 0; i < results.size(); i++) {
    for (size_t j = i + 1; j < results.size(); j++) {
      bool same = true;
      for (size_t k = 0; k < results[i].size(); k++) {
        if (results[i][k] != results[j][k]) {
          same = false;
          break;
        }
      }
      if (same) {
        allDifferent = false;
        std::cout << "  ERROR: Results " << i << " and " << j 
                  << " are identical!" << std::endl;
      }
    }
  }
  
  std::cout << "  All results different: " 
            << (allDifferent ? "PASSED" : "FAILED") << std::endl;
  
  // Test 4: Simulate your exact encoder behavior
  std::cout << "\nTest 4: Simulating Encoder's Linear Combination" << std::endl;
  
  // Create two packets with your actual test data
  std::vector<uint8_t> data1(16);
  std::vector<uint8_t> data2(16);
  for (int i = 0; i < 16; i++) {
    data1[i] = i;           // 0, 1, 2, ..., 15
    data2[i] = 16 - i;      // 16, 15, 14, ..., 1
  }
  
  // Two sets of coefficients from your test
  std::vector<uint8_t> encCoeffs1 = {103, 151};
  std::vector<uint8_t> encCoeffs2 = {14, 101};
  
  // Create coded packets exactly like your encoder does
  std::vector<uint8_t> codedData1(16, 0);
  std::vector<uint8_t> codedData2(16, 0);
  
  // Apply the encoding
  for (size_t i = 0; i < 16; i++) {
    uint8_t scaled1 = gf->Multiply(encCoeffs1[0], data1[i]);
    uint8_t scaled2 = gf->Multiply(encCoeffs1[1], data2[i]);
    codedData1[i] = gf->Add(scaled1, scaled2);
    
    uint8_t scaled3 = gf->Multiply(encCoeffs2[0], data1[i]);
    uint8_t scaled4 = gf->Multiply(encCoeffs2[1], data2[i]);
    codedData2[i] = gf->Add(scaled3, scaled4);
  }
  
  std::cout << "  First 8 bytes with coeffs [" << (int)encCoeffs1[0] << ", " 
            << (int)encCoeffs1[1] << "]: ";
  for (size_t i = 0; i < 8; i++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0') 
              << (int)codedData1[i] << " ";
  }
  std::cout << std::dec << std::endl;
  
  std::cout << "  First 8 bytes with coeffs [" << (int)encCoeffs2[0] << ", " 
            << (int)encCoeffs2[1] << "]: ";
  for (size_t i = 0; i < 8; i++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0') 
              << (int)codedData2[i] << " ";
  }
  std::cout << std::dec << std::endl;
  
  bool encoderResultsDifferent = false;
  for (size_t i = 0; i < 16; i++) {
    if (codedData1[i] != codedData2[i]) {
      encoderResultsDifferent = true;
      break;
    }
  }
  
  std::cout << "  Encoder results different: " 
            << (encoderResultsDifferent ? "PASSED" : "FAILED") << std::endl;
  
  if (!encoderResultsDifferent) {
    std::cout << "  ERROR: Different coefficients produced identical results!" << std::endl;
    std::cout << "  This matches the issue you're seeing in your encoder." << std::endl;
  }
}

// Helper function to print packet details safely (no VLAs)
void PrintPacketDetails(const std::string& label, Ptr<Packet> packet, bool showBytes = true) {
  std::cout << "=== " << label << " ===" << std::endl;
  std::cout << "Size: " << packet->GetSize() << " bytes" << std::endl;
  
  if (showBytes) {
    // Use vector instead of VLA
    std::vector<uint8_t> buffer(packet->GetSize());
    packet->CopyData(buffer.data(), packet->GetSize());
    
    std::cout << "Raw bytes: ";
    for (uint32_t i = 0; i < std::min(packet->GetSize(), (uint32_t)16); i++) {
      std::cout << std::hex << std::setw(2) << std::setfill('0') 
                << static_cast<int>(buffer[i]) << " ";
    }
    std::cout << std::dec << std::endl;
  }
  
  // Check if the packet has a network coding header
  // Only try to parse header if packet is large enough and likely to have one
  if (packet->GetSize() < 8) {
    std::cout << "Packet too small to have network coding header" << std::endl;
    return;
  }
  
  NetworkCodingHeader header;
  Ptr<Packet> copy = packet->Copy();
  bool hasHeader = false;
  
  try {
    uint32_t headerSize = copy->RemoveHeader(header);
    if (headerSize > 0 && headerSize <= packet->GetSize()) {
      hasHeader = true;
    }
  } catch (...) {
    // No valid header found
    hasHeader = false;
  }
  
  if (!hasHeader) {
    std::cout << "No valid network coding header found" << std::endl;
    return;
  }
  
  // Validate header values to ensure they make sense
  if (header.GetGenerationSize() == 0 || header.GetGenerationSize() > 255) {
    std::cout << "Invalid network coding header (bad generation size)" << std::endl;
    return;
  }
  
  std::cout << "Network Coding Header:" << std::endl;
  std::cout << "  Generation ID: " << header.GetGenerationId() << std::endl;
  std::cout << "  Generation Size: " << header.GetGenerationSize() << std::endl;
  
  const std::vector<uint8_t>& coeffs = header.GetCoefficients();
  std::cout << "  Coefficients: [";
  for (size_t i = 0; i < coeffs.size(); i++) {
    std::cout << static_cast<int>(coeffs[i]);
    if (i < coeffs.size() - 1) {
      std::cout << ", ";
    }
  }
  std::cout << "]" << std::endl;
  
  // Count non-zero coefficients
  int nonZero = 0;
  for (uint8_t coeff : coeffs) {
    if (coeff != 0) nonZero++;
  }
  std::cout << "  Non-zero coefficients: " << nonZero << std::endl;
  
  // Extract payload after header safely
  if (copy->GetSize() > 0) {
    std::vector<uint8_t> payloadBuffer(copy->GetSize());
    copy->CopyData(payloadBuffer.data(), copy->GetSize());
    
    std::cout << "  Payload: ";
    for (uint32_t i = 0; i < std::min(copy->GetSize(), (uint32_t)16); i++) {
      std::cout << std::hex << std::setw(2) << std::setfill('0') 
                << static_cast<int>(payloadBuffer[i]) << " ";
    }
    std::cout << std::dec << std::endl;
  }
  std::cout << std::endl;
}

// Simple test function
int main()
{
  std::cout << "Starting Network Coding Verification Test..." << std::endl;
  
  TestGaloisField();
  
  try {
    // Step 1: Create encoder with generation size 2, packet size 16
    std::cout << "Step 1: Creating encoder (gen size=2, packet size=16)..." << std::endl;
    Ptr<NetworkCodingEncoder> encoder = CreateObject<NetworkCodingEncoder>(2, 16);
    std::cout << "Encoder created successfully" << std::endl;
    
    // Step 2: Create and add packets to fill a generation
    std::cout << "Step 2: Creating and adding packets..." << std::endl;
    
    // Create packets with deterministic content for verification
    uint8_t data1[16];
    uint8_t data2[16];
    for (int i = 0; i < 16; i++) {
      data1[i] = i;          // 0, 1, 2, ..., 15
      data2[i] = 16 - i;     // 16, 15, 14, ..., 1
    }
    
    Ptr<Packet> packet1 = Create<Packet>(data1, 16);
    Ptr<Packet> packet2 = Create<Packet>(data2, 16);
    
    // Print original packet details
    PrintPacketDetails("Original Packet 1", packet1);
    PrintPacketDetails("Original Packet 2", packet2);
    
    bool added1 = encoder->AddPacket(packet1, 0);
    std::cout << "  Packet 1 added: " << (added1 ? "yes" : "no") << std::endl;
    
    bool added2 = encoder->AddPacket(packet2, 1);
    std::cout << "  Packet 2 added: " << (added2 ? "yes" : "no") << std::endl;
    
    // Step 3: Verify generation is complete
    bool complete = encoder->IsGenerationComplete();
    std::cout << "Step 3: Generation complete: " << (complete ? "yes" : "no") << std::endl;
    
    if (complete) {
      // Step 4: Generate coded packets
      std::cout << "Step 4: Generating coded packets..." << std::endl;
      
      for (int i = 0; i < 3; i++) {
        std::cout << "  Coded packet " << (i+1) << ":" << std::endl;
        
        Ptr<Packet> codedPacket = encoder->GenerateCodedPacket();
        if (!codedPacket) {
          std::cout << "    Failed to generate coded packet!" << std::endl;
          continue;
        }
        
        // Extract the header to inspect coefficients
        NetworkCodingHeader header;
        Ptr<Packet> copy = codedPacket->Copy();
        copy->RemoveHeader(header);
        
        std::cout << "    Packet size: " << codedPacket->GetSize() << " bytes" << std::endl;
        std::cout << "    Generation ID: " << header.GetGenerationId() << std::endl;
        std::cout << "    Generation size: " << header.GetGenerationSize() << std::endl;
        
        // Display coefficients
        const std::vector<uint8_t>& coeffs = header.GetCoefficients();
        std::cout << "    Coefficients: [";
        for (size_t j = 0; j < coeffs.size(); j++) {
          std::cout << static_cast<uint32_t>(coeffs[j]);
          if (j < coeffs.size() - 1) {
            std::cout << ", ";
          }
        }
        std::cout << "]" << std::endl;
        
        // Count non-zero coefficients
        int nonZero = 0;
        for (uint8_t coeff : coeffs) {
          if (coeff != 0) nonZero++;
        }
        std::cout << "    Non-zero coefficients: " << nonZero << std::endl;
        
        // Show a preview of the packet data
        std::vector<uint8_t> preview(8);
        copy->CopyData(preview.data(), std::min(copy->GetSize(), (uint32_t)8));
        std::cout << "    Data preview: ";
        for (uint32_t j = 0; j < std::min(copy->GetSize(), (uint32_t)8); j++) {
          std::cout << std::hex << std::setw(2) << std::setfill('0') 
                   << static_cast<int>(preview[j]) << " ";
        }
        std::cout << std::dec << std::endl;
      }
      
      // Step 5: Move to next generation
      std::cout << "Step 5: Moving to next generation..." << std::endl;
      encoder->NextGeneration();
      std::cout << "  New generation ID: " << encoder->GetCurrentGenerationId() << std::endl;
      
      // Step 6: Try to generate a packet from the new empty generation
      std::cout << "Step 6: Trying to generate coded packet from empty generation..." << std::endl;
      Ptr<Packet> emptyGenPacket = encoder->GenerateCodedPacket();
      if (emptyGenPacket) {
        std::cout << "  Unexpectedly generated a packet from empty generation!" << std::endl;
      } else {
        std::cout << "  Correctly returned nullptr for empty generation" << std::endl;
      }
      
      // Step 7: Add a packet to the new generation and generate a coded packet
      std::cout << "Step 7: Adding packet to new generation..." << std::endl;
      uint8_t data3[16];
      for (int i = 0; i < 16; i++) {
        data3[i] = i * 2;  // 0, 2, 4, ..., 30
      }
      Ptr<Packet> packet3 = Create<Packet>(data3, 16);
      bool added3 = encoder->AddPacket(packet3, 2);
      std::cout << "  Packet added to new generation: " << (added3 ? "yes" : "no") << std::endl;
      
      if (added3) {
        std::cout << "  Generating coded packet from incomplete generation..." << std::endl;
        Ptr<Packet> incompleteCoded = encoder->GenerateCodedPacket();
        if (incompleteCoded) {
          std::cout << "  Successfully generated coded packet from incomplete generation" << std::endl;
          
          // Extract and show the header
          NetworkCodingHeader header;
          incompleteCoded->RemoveHeader(header);
          
          std::cout << "    Generation ID: " << header.GetGenerationId() << std::endl;
          
          const std::vector<uint8_t>& coeffs = header.GetCoefficients();
          std::cout << "    Coefficients: [";
          for (size_t j = 0; j < coeffs.size(); j++) {
            std::cout << static_cast<uint32_t>(coeffs[j]);
            if (j < coeffs.size() - 1) {
              std::cout << ", ";
            }
          }
          std::cout << "]" << std::endl;
        } else {
          std::cout << "  Failed to generate coded packet from incomplete generation" << std::endl;
        }
      }
    }
    
    std::cout << "Test completed successfully" << std::endl;
  }
  catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
  catch (...) {
    std::cerr << "Unknown exception" << std::endl;
  }
  
  return 0;
}