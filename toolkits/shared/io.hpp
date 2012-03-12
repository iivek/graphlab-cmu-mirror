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
 *      http://graphlab.org
 *  
 */


#ifndef IO_HPP
#define IO_HPP



#include <omp.h>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>



#include "mmio.h"
#include "mathlayer.hpp"
#include "types.hpp"
#include <graphlab.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <graphlab/serialization/oarchive.hpp>
#include <graphlab/serialization/iarchive.hpp>
#include <cstdio>
#include <graphlab/macros_def.hpp>


extern bool debug;


enum matrix_market_parser{
   MATRIX_MARKET_3 = 1,
   MATRIX_MARKET_4 = 2,
   MATRIX_MARKET_5 = 3,
   MATRIX_MARKET_6 = 4
};



/*
 * open a file and verify open success
 */
FILE * open_file(const char * name, const char * mode, bool optional = false){
  FILE * f = fopen(name, mode);
  if (f == NULL && !optional){
      perror("fopen failed");
      logstream(LOG_FATAL) <<" Failed to open file" << name << std::endl;
   }
  return f;
}

FILE * open_file(const std::string name, const char * mode, bool optional = false){
   return open_file(name.c_str(), mode, optional);
}
/*
 * list all existing files inside a specificed directory
 */
std::vector<std::string> 
list_all_files_in_dir(const std::string & dir, const std::string &prefix){
  std::vector<std::string> result;
  graphlab::fs_util::list_files_with_prefix(dir, prefix, result);
  return result;
}

/*
 * extract the output from node data ito a vector of values
 */
template<typename graph_type>
vec  fill_output(graph_type * g, bipartite_graph_descriptor & matrix_info, int field_type){
  typedef typename graph_type::vertex_data_type vertex_data_type;

  vec out = zeros(matrix_info.num_nodes(false));
  for (int i = matrix_info.get_start_node(false); i < matrix_info.get_end_node(false); i++){
    out[i] = g->vertex_data(i).get_output(field_type);
  }
  return out;
}

template<typename Graph>
struct matrix_entry {
  typedef Graph graph_type;
  typedef typename graph_type::vertex_id_type vertex_id_type;
  typedef typename graph_type::edge_data_type edge_data_type;
  vertex_id_type source, target;
  edge_data_type edata;
  matrix_entry() : source(0), target(0) { }
  matrix_entry(const vertex_id_type& source, 
               const vertex_id_type& target,
               const edge_data_type& edata) :
    source(source), target(target), edata(edata) { }
}; // end of matrix_entry



template<typename Graph>
bool load_matrixmarket(const std::string& fname,
                       bipartite_graph_descriptor& desc,
                       std::vector< matrix_entry<Graph> >& test_set) {
  typedef Graph graph_type;
  typedef typename graph_type::vertex_id_type vertex_id_type;
  typedef typename graph_type::edge_data_type edge_data_type;
  typedef matrix_entry<graph_type> matrix_entry_type;

  // Open the file 
  FILE* fptr = open_file(fname.c_str(), "r");
  
  // read Matrix market header
  MM_typecode matcode;
  if(mm_read_banner(fptr, &matcode)) {
    logstream(LOG_FATAL) << "Unable to read banner" << std::endl;
  }
  // Screen header type
  if (mm_is_complex(matcode) || !mm_is_matrix(matcode)) {
    logstream(LOG_FATAL) 
      << "Sorry, this application does not support matrixmarket type: "
      <<  mm_typecode_to_str(matcode) << std::endl;
  }
  // load the matrix descriptor
  if(mm_read_mtx_crd_size(fptr, &desc.rows, &desc.cols, &desc.nonzeros)) {
    logstream(LOG_ERROR) << "Error reading dimensions" << std::endl;
  }
  std::cout << "Rows:      " << desc.rows << std::endl
            << "Cols:      " << desc.cols << std::endl
            << "Nonzeros:  " << desc.nonzeros << std::endl;
  std::cout << "Constructing all vertices." << std::endl;
   std::cout << "Adding edges." << std::endl;
  for(size_t i = 0; i < size_t(desc.nonzeros); ++i) {    
    int row = 0, col = 0;  double val = 0;
    if(fscanf(fptr, "%d %d %lg\n", &row, &col, &val) != 3) {
      logstream(LOG_FATAL) 
        << "Error reading file on line: " << i << std::endl;
    } --row; --col;
    ASSERT_LT(row, desc.rows);
    ASSERT_LT(col, desc.cols);
    const vertex_id_type source = row;
    const vertex_id_type target = col + desc.rows;
    const edge_data_type edata(val);
    test_set.push_back(matrix_entry_type(source, target, edata));
  } // end of for loop  
  return true;
} // end of load matrixmarket graph

template<typename Graph>
bool load_matrixmarket_cpp_graph(const std::string& fname,
                             bipartite_graph_descriptor& desc,
                             Graph& graph,
		             bool gzip = false,
			     int parse_type = MATRIX_MARKET_3){ 
  typedef Graph graph_type;
  typedef typename graph_type::vertex_id_type vertex_id_type;
  typedef typename graph_type::edge_data_type edge_data_type;
  typedef matrix_entry<graph_type> matrix_entry_type;

  // Open the file 
  logstream(LOG_INFO) << "Reading matrix market file: " << fname << std::endl;
  //FILE* fptr = open_file(fname.c_str(), "r");
  std::ifstream in_file(fname.c_str(), std::ios::binary);
  boost::iostreams::filtering_stream<boost::iostreams::input> fin;
  if (gzip)
    fin.push(boost::iostreams::gzip_decompressor());
  fin.push(in_file); 
    
  MM_typecode matcode;
  int tmprows, tmpcols;
  size_t tmpnnz;

    // read Matrix market header
    if(mm_read_cpp_banner(fin, &matcode)) {
      logstream(LOG_FATAL) << "Unable to read banner" << std::endl;
    }
    // Screen header type
    if (mm_is_complex(matcode) || !mm_is_matrix(matcode)) {
      logstream(LOG_FATAL) 
      << "Sorry, this application does not support matrixmarket type: "
      <<  mm_typecode_to_str(matcode) << std::endl;
     return false;
    }
    // load the matrix descriptor
    if(mm_read_cpp_mtx_crd_size(fin, &tmprows, &tmpcols, &tmpnnz)) {
      logstream(LOG_FATAL) << "Error reading dimensions" << std::endl;
    }

  //update dataset size from file only with empty structure
  if (desc.rows == 0 && desc.cols == 0 && desc.nonzeros == 0){
     desc.rows = tmprows; desc.cols = tmpcols; desc.nonzeros = tmpnnz;
  }

  std::cout << "Rows:      " << desc.rows << std::endl
            << "Cols:      " << desc.cols << std::endl
            << "Nonzeros:  " << desc.nonzeros << std::endl;
  std::cout << "Constructing all vertices." << std::endl;

  if ((int)graph.num_vertices() < desc.total())
    graph.resize(desc.total());
  bool is_square = desc.is_square();

  char line[MM_MAX_LINE_LENGTH];

  std::cout << "Adding edges." << std::endl;
  for(size_t i = 0; i < size_t(desc.nonzeros); ++i) {    
    int row = 0, col = 0, inttime = 0, intdate = 0;  
    double val = 0;
    unsigned long long time = 0;
	

    if (fin.eof()){
       if (i < (size_t)(desc.nonzeros - 1))
            logstream(LOG_WARNING) <<"File " << fname << " ended after " << i << " expected lines: " << desc.nonzeros << std::endl;
       break;
    }
    fin.getline(line, MM_MAX_LINE_LENGTH);
    

    //regular matrix market format. [from] [to] [val]
    if (parse_type == MATRIX_MARKET_3){ 
        if(sscanf(line, "%d %d %lg\n", &row, &col, &val) != 3) {
        logstream(LOG_ERROR) 
          << "Error reading file on line: " << i << std::endl;
        return false;
      }
    }
     //extended matrix market format. [from] [to] [val from->to] [val to->from] [ignored] [ignored]
    else if (parse_type == MATRIX_MARKET_4){ 
        if(sscanf(line, "%d %d %llu %lg\n", &row, &col, &time, &val) != 4) {
        logstream(LOG_ERROR) 
          << "Error reading file on line: " << i << std::endl;
        return false;
      }
    }
     //extended matrix market format. [from] [to] [val from->to] [val to->from] [ignored] [ignored]
    else if (parse_type == MATRIX_MARKET_5){ 
        if(sscanf(line, "%d %d %d %d %lg\n", &row, &col, &intdate, &inttime, &val) != 5) {
        logstream(LOG_ERROR) 
          << "Error reading file on line: " << i << std::endl;
        return false;
      }
     //extended matrix market format. [from] [to] [val from->to] [val to->from] [ignored] [ignored]
    }  else if (parse_type == MATRIX_MARKET_6){
      double val2, zero, zero1;
      if(sscanf(line, "%d %d %lg %lg %lg %lg\n", &row, &col, &val, &val2, &zero, &zero1) != 6) {
        logstream(LOG_FATAL) 
          << "Error reading file " << fname << " on line: " << i << std::endl;
        return false;
      }
      val += val2; //sum up to values to have a single undirected link
    }
    else assert(false);

    --row; --col;

    ASSERT_LT(row, desc.rows);
    ASSERT_LT(col, desc.cols);
    ASSERT_GE(row, 0);
    ASSERT_GE(col, 0);
    const vertex_id_type source = row;
    const vertex_id_type target = col + (is_square ? 0 : desc.rows);
    const edge_data_type edata(val);

    if (debug && desc.nonzeros < 100)
      logstream(LOG_INFO)<<"Adding an edge: " << source << "->" << target << " with val: " << val << std::endl;

    if(is_square && source == target) 
      graph.vertex_data(source).add_self_edge(val);
    else {
     graph.add_edge(source, target, edata); 
      if (mm_is_symmetric(matcode))
        graph.add_edge(target, source, edata);
    }
  } // end of for loop  
  std::cout << "Graph size:    " << graph.num_edges() << std::endl;
  //graph.finalize();
  
   if (gzip)
     fin.pop();
   fin.pop();
   in_file.close();
   return true;
} // end of load matrixmarket graph


template<typename Graph>
bool load_matrixmarket_graph(const std::string& fname,
                             bipartite_graph_descriptor& desc,
                             Graph& graph,
			     int parse_type = MATRIX_MARKET_3, 
			     bool allow_zeros = false, 
			     bool header_only = false){ 
  typedef Graph graph_type;
  typedef typename graph_type::vertex_id_type vertex_id_type;
  typedef typename graph_type::edge_data_type edge_data_type;
  typedef matrix_entry<graph_type> matrix_entry_type;

  // Open the file 
  logstream(LOG_INFO) << "Reading matrix market file: " << fname << std::endl;
  FILE* fptr = open_file(fname.c_str(), "r");
  
  // read Matrix market header
  MM_typecode matcode;
  if(mm_read_banner(fptr, &matcode)) {
    logstream(LOG_FATAL) << "Unable to read banner" << std::endl;
  }
  // Screen header type
  if (mm_is_complex(matcode) || !mm_is_matrix(matcode)) {
    logstream(LOG_FATAL) 
      << "Sorry, this application does not support matrixmarket type: "
      <<  mm_typecode_to_str(matcode) << std::endl;
    return false;
  }
  // load the matrix descriptor
  if(mm_read_mtx_crd_size(fptr, &desc.rows, &desc.cols, &desc.nonzeros)) {
    logstream(LOG_FATAL) << "Error reading dimensions" << std::endl;
  }
  std::cout << "Rows:      " << desc.rows << std::endl
            << "Cols:      " << desc.cols << std::endl
            << "Nonzeros:  " << desc.nonzeros << std::endl;
  std::cout << "Constructing all vertices." << std::endl;
  graph.resize(desc.total());
  bool is_square = desc.is_square();

   if (header_only)
      return true;

  std::cout << "Adding edges." << std::endl;
  for(size_t i = 0; i < size_t(desc.nonzeros); ++i) {    
    int row = 0, col = 0;  
    double val = 0;

    //regular matrix market format. [from] [to] [val]
    if (parse_type == MATRIX_MARKET_3){ 
      if(fscanf(fptr, "%d %d %lg\n", &row, &col, &val) != 3) {
        logstream(LOG_ERROR) 
          << "Error reading file on line: " << i << std::endl;
        return false;
      }
     //extended matrix market format. [from] [to] [val from->to] [val to->from] [ignored] [ignored]
    } else if (parse_type == MATRIX_MARKET_6){
      double val2, zero, zero1;
      if(fscanf(fptr, "%d %d %lg %lg %lg %lg\n", &row, &col, &val, &val2, &zero, &zero1) != 6) {
        logstream(LOG_FATAL) 
          << "Error reading file " << fname << " on line: " << i << std::endl;
        return false;
      }
      val += val2; //sum up to values to have a single undirected link
    }
    else assert(false);

    --row; --col;

    ASSERT_LT(row, desc.rows);
    ASSERT_LT(col, desc.cols);
    ASSERT_GE(row, 0);
    ASSERT_GE(col, 0);

   if (val == 0){
     if (!allow_zeros)
       logstream(LOG_FATAL)<<"Zero values are not allowed in sparse matrix market format. Use --zero=true to ignore this error." <<std::endl;
     else 
       continue;
    }
    const vertex_id_type source = row;
    const vertex_id_type target = col + (is_square ? 0 : desc.rows);
    const edge_data_type edata(val);

    if (debug && desc.nonzeros < 100)
      logstream(LOG_INFO)<<"Adding an edge: " << source << "->" << target << " with val: " << std::endl;

    if(is_square && source == target) 
      graph.vertex_data(source).add_self_edge(val);
    else {
     graph.add_edge(source, target, edata); 
      if (mm_is_symmetric(matcode))
        graph.add_edge(target, source, edata);
    }
  } // end of for loop  
  std::cout << "Graph size:    " << graph.num_edges() << std::endl;
  //graph.finalize();
  return true;
} // end of load matrixmarket graph




template<typename Graph>
bool load_graph(const std::string& fname,
                const std::string& format,
                bipartite_graph_descriptor& desc,
                Graph& graph, 
	        int format_type = MATRIX_MARKET_3, 
	        bool allow_zeros=false, 
	        bool header_only=false) {

  if(format == "matrixmarket") 
    return load_matrixmarket_graph(fname, desc, graph, format_type, allow_zeros, header_only);
  else logstream(LOG_FATAL) << "Invalid file format!" << std::endl;
  return false;
} // end of load graph

template<typename Graph>
bool load_cpp_graph(const std::string& fname,
                const std::string& format,
                bipartite_graph_descriptor& desc,
                Graph& graph, 
	        bool gzip = false,
	        int format_type = MATRIX_MARKET_3) {

  if(format == "matrixmarket") 
    return load_matrixmarket_cpp_graph(fname, desc, graph, gzip, format_type);
  else std::cout << "Invalid file format!" << std::endl;
  return false;
} // end of load graph


template <typename graph_type>
void load_matrix_market_vector(const std::string & filename, const bipartite_graph_descriptor & desc, graph_type & g, int type, bool optional_field, bool allow_zeros)
{
    typedef typename graph_type::vertex_data_type vertex_data;
    
    int ret_code;
    MM_typecode matcode;
    int M, N; 
    size_t i,nz;  

    logstream(LOG_INFO) <<"Going to read matrix market vector from input file: " << filename << std::endl;
  
    FILE * f = open_file(filename.c_str(), "r", optional_field);
    //if optional file not found return
    if (f== NULL && optional_field){
       return;
    }

    if (mm_read_banner(f, &matcode) != 0)
        logstream(LOG_FATAL) << "Could not process Matrix Market banner." << std::endl;

    /*  This is how one can screen matrix types if their application */
    /*  only supports a subset of the Matrix Market data types.      */

    if (mm_is_complex(matcode) && mm_is_matrix(matcode) && 
            mm_is_sparse(matcode) )
        logstream(LOG_FATAL) << "sorry, this application does not support " << std::endl << 
          "Market Market type: " << mm_typecode_to_str(matcode) << std::endl;

    /* find out size of sparse matrix .... */
    if (mm_is_sparse(matcode)){
       if ((ret_code = mm_read_mtx_crd_size(f, &M, &N, &nz)) !=0)
          logstream(LOG_FATAL) << "failed to read matrix market cardinality size " << std::endl; 
    }
    else {
      if ((ret_code = mm_read_mtx_array_size(f, &M, &N))!= 0)
          logstream(LOG_FATAL) << "failed to read matrix market vector size " << std::endl; 
         if (N > M){ //if this is a row vector, transpose
           int tmp = N;
           N = M;
           M = tmp;
         }
         nz = M*N;
    }

    /* NOTE: when reading in doubles, ANSI C requires the use of the "l"  */
    /*   specifier as in "%lg", "%lf", "%le", otherwise errors will occur */
    /*  (ANSI C X3.159-1989, Sec. 4.9.6.2, p. 136 lines 13-15)            */

    int row,col; 
    double val;

    for (i=0; i<nz; i++)
    {
        if (mm_is_sparse(matcode)){
          int rc = fscanf(f, "%d %d %lg\n", &row, &col, &val);
          if (rc != 3){
	    logstream(LOG_FATAL) << "Failed reading input file: " << filename << "Problm at data row " << i << " (not including header and comment lines)" << std::endl;
          }
          row--;  /* adjust from 1-based to 0-based */
          col--;
        }
        else {
	  int rc = fscanf(f, "%lg\n", &val);
          if (rc != 1){
	    logstream(LOG_FATAL) << "Failed reading input file: " << filename << "Problm at data row " << i << " (not including header and comment lines)" << std::endl;
          }
          row = i;
          col = 0;
        }
       //some users have gibrish in text file - better check both I and J are >=0 as well
        assert(row >=0 && row< M);
        assert(col == 0);
        if (val == 0 && !allow_zeros)
           logstream(LOG_FATAL)<<"Zero entries are not allowed in a sparse matrix market vector. Use --zero=true to avoid this error"<<std::endl;
        //set observation value
        vertex_data & vdata = g.vertex_data(row);
        vdata.set_val(val, type);
    }
    fclose(f);

}


template<typename Graph>
void load_vector(const std::string& fname,
                   const std::string& format,
                   const bipartite_graph_descriptor& desc,
                   Graph& graph, 
		   int type,
		   bool optional_field, 
		   bool allow_zeros) {

  if (format == "matrixmarket"){
     load_matrix_market_vector(fname, desc, graph, type, optional_field, allow_zeros);
     return;
  }
  else assert(false); //TODO other formats

}

inline void write_row(int row, int col, double val, FILE * f, bool issparse){
    if (issparse)
      fprintf(f, "%d %d %10.13g\n", row, col, val);
    else fprintf(f, "%10.13g ", val);
}

inline void write_row(int row, int col, int val, FILE * f, bool issparse){
    if (issparse)
      fprintf(f, "%d %d %d\n", row, col, val);
    else fprintf(f, "%d ", val);
}

template<typename T>
inline void set_typecode(MM_typecode & matcore);

template<>
inline void set_typecode<vec>(MM_typecode & matcode){
   mm_set_real(&matcode);
}

template<>
inline void set_typecode<ivec>(MM_typecode & matcode){
  mm_set_integer(&matcode);
}


template<typename vec>
void save_matrix_market_format_vector(const std::string datafile, const vec & output, bool issparse, std::string comment)
{
    MM_typecode matcode;                        
    mm_initialize_typecode(&matcode);
    mm_set_matrix(&matcode);
    mm_set_coordinate(&matcode);

    if (issparse)
       mm_set_sparse(&matcode);
    else mm_set_dense(&matcode);

    set_typecode<vec>(matcode);

    FILE * f = fopen(datafile.c_str(),"w");
    assert(f != NULL);
    mm_write_banner(f, matcode); 
    if (comment.size() > 0) // add a comment to the matrix market header
      fprintf(f, "%c%s\n", '%', comment.c_str());
    if (issparse)
    mm_write_mtx_crd_size(f, output.size(), 1, output.size());
    else
    mm_write_mtx_array_size(f, output.size(), 1);

    for (int j=0; j<(int)output.size(); j++){
      write_row(j+1, 1, output[j], f, issparse);
      if (!issparse) 
        fprintf(f, "\n");
    }

    fclose(f);
}

template<typename mat>
void save_matrix_market_format_matrix(const std::string datafile, const mat & output, bool issparse)
{
    MM_typecode matcode;                        
    mm_initialize_typecode(&matcode);
    mm_set_matrix(&matcode);
    if (issparse)
      mm_set_coordinate(&matcode);
    else mm_set_array(&matcode);

    if (issparse)
      mm_set_sparse(&matcode);
    else mm_set_dense(&matcode);

    set_typecode<vec>(matcode);

    FILE * f = fopen(datafile.c_str(),"w");
    assert(f != NULL);
    mm_write_banner(f, matcode); 
    if (issparse)
      mm_write_mtx_crd_size(f, output.size(), 1, output.size());
    else 
      mm_write_array_size(f, output.size(), 1);
    for (int j=0; j<(int)output.rows(); j++)
      for (int i=0; i< (int)output.cols(); i++){
         write_row(j+1, i+1, get_val(output, i, j), f, issparse);
         if (!issparse && (i == (int)output.cols() - 1))
           fprintf(f, "\n");
      }

    fclose(f);
}



//read a vector from file and return an array
template<typename T>
inline T * read_vec(FILE * f, int len){
  T * vec = new T[len];
  assert(vec != NULL);
  int total = 0;
  while (total < len){
    int rc = fread(vec, sizeof(T), len, f); 
    if (rc <= 0){
      perror("fread");
      logstream(LOG_FATAL)<<"Failed reading array" << std::endl;
    }
   total += rc;
  }
  assert(total == (int)len);
  return vec;
}


//write an output vector to file
template<typename T>
inline void write_vec(const FILE * f, const int len, const T * array){
  int total = 0;
  assert(f != NULL && array != NULL);
  while(total < len){
     int rc = fwrite(array + total, sizeof(T), len - total, (FILE*)f);
    if (rc <= 0){
      perror("fwrite");
      logstream(LOG_FATAL) << "Failed writing array" << std::endl; 
    }
    total += rc;
  }
  assert(total == len);
}

//write an output vector to file
/*inline void write_vec(const FILE * f, const int len, const int * array){
  assert(f != NULL && array != NULL);
  int rc = fwrite(array, sizeof(int), len, (FILE*)f);
  assert(rc == len);
}*/

template<typename T>
inline void write_output_vector_binary(const std::string & datafile, const T* output, int size){

   FILE * f = open_file(datafile.c_str(), "w");
   std::cout<<"Writing result to file: "<<datafile<<std::endl;
   write_vec(f, size, output);
   fclose(f);
}

class gzip_in_file{
  boost::iostreams::filtering_stream<boost::iostreams::input> fin;
  std::ifstream in_file;

  public:
  gzip_in_file(const std::string filename){
    in_file.open(filename.c_str(), std::ios::binary);
    logstream(LOG_INFO)<<"Opening input file: " << filename << std::endl;
    fin.push(boost::iostreams::gzip_decompressor());
    fin.push(in_file);  
   }

  ~gzip_in_file(){
    fin.pop(); fin.pop();
    in_file.close();
   }

   boost::iostreams::filtering_stream<boost::iostreams::input> & get_sp(){
     return fin;
   }


};


class gzip_out_file{

  boost::iostreams::filtering_stream<boost::iostreams::output> fout;
  std::ofstream out_file;
  
  public:
      gzip_out_file(const std::string filename){
        out_file.open(filename.c_str(), std::ios::binary);
        logstream(LOG_INFO)<<"Opening output file " << filename << std::endl;
        fout.push(boost::iostreams::gzip_compressor());
        fout.push(out_file);
      }
     
      boost::iostreams::filtering_stream<boost::iostreams::output> & get_sp(){
        return fout;
      }

      ~gzip_out_file(){
        fout.pop(); fout.pop();
        out_file.close();
      }
};

template<typename vec>
inline void write_output_vector_binary(const std::string & datafile, const vec& output){

   FILE * f = open_file(datafile.c_str(), "w");
   std::cout<<"Writing result to file: "<<datafile<<std::endl;
   std::cout<<"You can read the file in Matlab using the load_c_gl.m matlab script"<<std::endl;
   write_vec(f, output.size(), &output[0]);
   fclose(f);
}
template<typename vec>
inline vec* read_input_vector_binary(const std::string & datafile, int len){

   FILE * f = open_file(datafile.c_str(), "r");
   std::cout<<"Reading binary vector from file: "<<datafile<<std::endl;
   vec * input = read_vec<vec>(f, len);
   fclose(f);
   return input;
}


template<typename mat>
inline void write_output_matrix_binary(const std::string & datafile, const mat& output){

   FILE * f = open_file(datafile.c_str(), "w");
   std::cout<<"Writing result to file: "<<datafile<<std::endl;
   std::cout<<"You can read the file in Matlab using the load_c_gl.m matlab script"<<std::endl;
   write_vec(f, output.size(), data(output));
   fclose(f);
}


template<typename vec>
inline void write_output_vector(const std::string & datafile, const std::string & format, const vec& output, bool issparse, std::string comment = ""){

  logstream(LOG_INFO)<<"Going to write output to file: " << datafile << " (vector of size: " << output.size() << ") " << std::endl;
  if (format == "binary")
    write_output_vector_binary(datafile, output);
  else if (format == "matrixmarket")
    save_matrix_market_format_vector(datafile, output,issparse, comment); 
  else assert(false);
}

template<typename mat>
inline void write_output_matrix(const std::string & datafile, const std::string & format, const mat& output, bool isparse){

  if (format == "binary")
    write_output_matrix_binary(datafile, output);
  else if (format == "matrixmarket")
    save_matrix_market_format_matrix(datafile, output); 
  else assert(false);
}


template<typename T1>
void save_map_to_file(const T1 & map, const std::string filename){
    logstream(LOG_INFO)<<"Save map to file: " << filename << " map size: " << map.size() << std::endl;
    std::ofstream ofs(filename.c_str());
    graphlab::oarchive oa(ofs);
    oa << map;
}

template<typename T1>
void load_map_from_file(T1 & map, const std::string filename){
   logstream(LOG_INFO)<<"loading map from file: " << filename << std::endl;
   std::ifstream ifs(filename.c_str());
   graphlab::iarchive ia(ifs);
   ia >> map;
   logstream(LOG_INFO)<<"Map size is: " << map.size() << std::endl;
 }

//read matrix size from a binary file
FILE * load_matrix_metadata(const char * filename, bipartite_graph_descriptor & desc){
   printf("Loading %s\n", filename);
   FILE * f = open_file(filename, "r", false);

   int rc = fread(&desc.rows, sizeof(desc.rows), 1, f);
   assert(rc == 1);
   rc = fread(&desc.cols, sizeof(desc.cols), 1, f);
   assert(rc == 1);
   return f;
}


template<typename Graph>
bool load_binary_graph(const std::string& fname,
                             bipartite_graph_descriptor& desc,
                             Graph& graph) {
  typedef Graph graph_type;
  typedef typename graph_type::vertex_id_type vertex_id_type;
  typedef typename graph_type::edge_data_type edge_data_type;
  typedef matrix_entry<graph_type> matrix_entry_type;

  // Open the file 
  logstream(LOG_INFO) << "Reading matrix market file: " << fname << std::endl;
  FILE* fptr = open_file(fname.c_str(), "r");
  
  // read Matrix market header
  MM_typecode matcode;
  if(mm_read_banner(fptr, &matcode)) {
    logstream(LOG_ERROR) << "Unable to read banner" << std::endl;
    return false;
  }
  // Screen header type
  if (mm_is_complex(matcode) || !mm_is_matrix(matcode)) {
    logstream(LOG_ERROR) 
      << "Sorry, this application does not support matrixmarket type: "
      <<  mm_typecode_to_str(matcode) << std::endl;
    return false;
  }
  // load the matrix descriptor
  if(mm_read_mtx_crd_size(fptr, &desc.rows, &desc.cols, &desc.nonzeros)) {
    logstream(LOG_ERROR) << "Error reading dimensions" << std::endl;
  }
  std::cout << "Rows:      " << desc.rows << std::endl
            << "Cols:      " << desc.cols << std::endl
            << "Nonzeros:  " << desc.nonzeros << std::endl;
  std::cout << "Constructing all vertices." << std::endl;
  graph.resize(desc.total());
  bool is_square = desc.is_square();

  std::cout << "Adding edges." << std::endl;
  int step = 0;
  if (desc.nonzeros > 10000000)
     step = desc.nonzeros / 100;

  for(size_t i = 0; i < size_t(desc.nonzeros); ++i) {    
    int row = 0, col = 0;  
    double val = 0;
    if (step > 0 && (i % step == 0))
       logstream(LOG_INFO) << "Loaded " << i << " edges for far." << std::endl;

    if(fscanf(fptr, "%d %d %lg\n", &row, &col, &val) != 3) {
      logstream(LOG_FATAL) 
        << "Error reading file " << fname << " on line: " << i << std::endl;
    } --row; --col;
    ASSERT_LT(row, desc.rows);
    ASSERT_LT(col, desc.cols);
    ASSERT_GE(row, 0);
    ASSERT_GE(col, 0);
    const vertex_id_type source = row;
    const vertex_id_type target = col + (is_square ? 0 : desc.rows);
    const edge_data_type edata(val);

    if (debug && desc.nonzeros < 100)
      logstream(LOG_INFO)<<"Adding an edge: " << source << "->" << target << " with val: " << std::endl;

    if(is_square && source == target) 
      graph.vertex_data(source).add_self_edge(val);
    else
      graph.add_edge(source, target, edata);
  } // end of for loop  
  std::cout << "Graph size:    " << graph.num_edges() << std::endl;
  //graph.finalize();
  return true;
} // end of load matrixmarket graph


uint mmap_from_file(std::string filename, uint *& array){
          struct stat sb;
          int fd = open (filename.c_str(), O_RDONLY);
          if (fd == -1) {
                  perror ("open");
                  logstream(LOG_FATAL) << "Failed to open input file: " << filename << std::endl;
          }

          if (fstat (fd, &sb) == -1) {
                  perror ("fstat");
                  logstream(LOG_FATAL) << "Failed to get size of  input file: " << filename << std::endl;
          }

          if (!S_ISREG (sb.st_mode)) {
                  logstream(LOG_FATAL) << "Input file: " << filename 
              << " is not a regular file and can not be mapped " << std::endl;
          }

          array = (uint*)mmap (0, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
          if (array == MAP_FAILED) {
                  perror ("mmap");
                  logstream(LOG_FATAL) << "Failed to map input file: " << filename << std::endl;
          }
          return sb.st_size;
}



// type Graph should be graph2
template <typename Graph>
void save_to_bin(const std::string &filename, Graph& graph, bool edge_weight) {
  typedef typename Graph::vertex_id_type vertex_id_type;
  typedef typename Graph::edge_id_type edge_id_type;
  typedef typename Graph::edge_list_type edge_list_type;

  uint* nodes = new uint[graph.num_vertices()+1];
  uint* innodes = new uint[graph.num_vertices()+1];
  nodes[0] = 0; innodes[0] = 0;
  double * in_weights = NULL;
  if (edge_weight)
     in_weights = new double[graph.num_edges()]; 

  int cnt = 0; 
  for (int i=0; i< (int)graph.num_vertices(); i++){
     nodes[i+1] = nodes[i]+ graph.num_out_edges(i); 
     innodes[i+1] = innodes[i] + graph.num_in_edges(i);
     if (edge_weight){
        const edge_list_type ins = graph.in_edges(i);
        for (uint j=0; j< ins.size(); j++){
           in_weights[cnt] = graph.edge_data(ins[j]).val;
	   cnt++;
        }
     } 
     assert(nodes[i+1] <= graph.num_edges());
     assert(innodes[i+1] <= graph.num_edges());
   };
   if (edge_weight)
     assert((uint)cnt == graph.num_edges());
 
  write_output_vector_binary(filename + ".nodes", nodes, graph.num_vertices()+1);
  write_output_vector_binary(filename + "-r.nodes", innodes, graph.num_vertices()+1);

#ifdef USE_GRAPH3
  uint* _edges = graph.get_node_out_edges();
  uint* _inedges = graph.get_node_in_edges();
#else
  typedef typename Graph::edge_data_type _edge_data_type;
  const std::vector<edge_id_type>& _edges = graph.get_out_edge_storage();
  const std::vector<edge_id_type>& _inedges = graph.get_in_edge_storage();
  if (edge_weight){  
  const std::vector<_edge_data_type>& _weights = graph.get_edge_data_storage();
    write_output_vector_binary(filename + ".weights", (double*)&_weights[0], graph.num_edges());
    write_output_vector_binary(filename + "-r.weights", in_weights, graph.num_edges());
  }
#endif
  write_output_vector_binary(filename + ".edges", &_edges[0], graph.num_edges());
  write_output_vector_binary(filename + "-r.edges", &_inedges[0], graph.num_edges());

  delete[] nodes;
  delete [] innodes;
}


#include <graphlab/macros_undef.hpp>
#endif // end of matrix_loader_hpp