/**  
 * Copyright (c) 2009 Carnegie Mellon University. 
 *     All rights reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS
 *  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied.  See the License for the specific language
 *  governing permissions and limitations under the License.
 *
 * For more about this software visit:
 *
 *      http://www.graphlab.ml.cmu.edu
 *
 */


#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <cstring>

#include <graphlab/logger/assertions.hpp>

#include <graphlab/serialization/serialize.hpp>
#include <graphlab/serialization/vector.hpp>
#include <graphlab/serialization/map.hpp>
#include <graphlab/serialization/list.hpp>
#include <graphlab/serialization/set.hpp>
#include <graphlab/util/generics/any.hpp>
#include <graphlab/util/generics/any_vector.hpp>

using namespace graphlab;



struct TestClass1{
  int z;
  void save(oarchive &a) const {
    a << z;
  }
  void load(iarchive &a) {
    a >> z;
  }
};



class TestClass2{
public:
  int i;
  int j;
  std::vector<int> k;
  TestClass1 l;
  void save(oarchive &a) const {
    a << i << j << k << l;
  }
  void load(iarchive &a) {
    a >> i >> j >> k >> l;
  }
};



class TestClass3{
public:
  int i;
  int j;
  std::vector<int> k;
};


void is10(const graphlab::any a) {
  ASSERT_EQ(a.as<int>(), 10);
}


void test_any_vector() {  
  any_vector vec(10, size_t(3));
  ASSERT_EQ(vec.size(), 10);
  for(size_t i = 0; i < vec.size(); ++i) {
    graphlab::any value = vec.get(i);
    ASSERT_EQ(value.as<size_t>(), 3);
    ASSERT_EQ(vec.as<size_t>(i), 3);
    ASSERT_EQ(vec.as<size_t>()[i], 3);
    vec.as<size_t>(i) = i;
  }
  std::stringstream ostrm;
  oarchive oarc(ostrm);
  oarc << vec;
  std::stringstream istrm(ostrm.str());
  iarchive iarc(istrm);
  any_vector vec2;
  iarc >> vec2;
  for(size_t i = 0; i < vec.size(); ++i) {
    graphlab::any value = vec2.get(i);
    ASSERT_EQ(value.as<size_t>(), i);
    ASSERT_EQ(vec2.as<size_t>(i), i);
    ASSERT_EQ(vec2.as<size_t>()[i], i);
  }

  any_vector vec3 = vec2;
  std::cout << vec3 << std::endl;
  
}

int main(int argc, char** argv) {
  std::cout << "Beginning anytests" << std::endl;
  global_logger().set_log_level(LOG_INFO);
  global_logger().set_log_to_console(true);
  std::ofstream f;
  f.open("test.bin",std::fstream::binary);
  oarchive oarc(f);
  graphlab::any variant;

  // store a bunch of stuff
  int i = 10;
  variant = i;
  ASSERT_EQ(variant.as<int>(), 10);
  is10(variant);
  oarc << variant;
  
  
  double d = 3.14159;
  variant = d;
  ASSERT_GT(variant.as<double>(), 3.14158);
  ASSERT_LE(variant.as<double>(), 3.1416);
  oarc << variant;

  TestClass1 t;
  t.z = 4321;
  variant = t;
  ASSERT_EQ(variant.as<TestClass1>().z, 4321);
  oarc << variant;

  TestClass2 t2;
  t2.i = 1;
  t2.j = 2;
  t2.l.z = 3;
  for (int i = 0;i < 10; ++i) {
    t2.k.push_back(i);
  }
  variant = t2;
  oarc << variant;
  f.close();
  
  TestClass3 t3;
  any tmp(t3);

  test_any_vector();


  std::cout << "Finished anytests" << std::endl;
}
