#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <cassert>
#include <stdexcept>
#include "graph.h"

extern "C" {
#include <unistd.h>
}

#ifndef HAVE_GETOPT_H
#include <getopt.h>
#endif

#if defined HAVE_GETOPT_H && defined HAVE_GETOPT_LONG
#include <getopt.h>
#else
extern "C" {
#include "getopt.h"
}
#endif

#define PREC_DEFAULT 8
#define EPS_DEFAULT 1.0e-9
#define HELP " [-m iter -e eps -p prec] -i matrix -l label -r output"
#define ITER_DEFAULT 10000

namespace ssl_lprop {

class LP {
 public:
  LP () : eps_(1.0e-9), N_(0), L_(0), U_(0) {};
  explicit LP (double eps) : eps_(eps), N_(0), L_(0), U_(0) {};
  ~LP () { clear(); }

  void clear();
  bool read(const std::string mfilename,
            const std::string lfilename);
  bool train(const int max_iter);
  bool write(const char* filename,
             const unsigned int prec);
  void show(const unsigned int prec);

 private:
  unsigned int N_; // size of nodes
  unsigned int L_; // size of labeled nodes
  unsigned int U_; // size of unlabeled nodes
  unsigned int C_;
  double eps_;
  ssl_lprop::Matrix trans; // transition matrix
  ssl_lprop::Matrix norm; // row-normalized transition matrix
  ssl_lprop::Labels labels;
  ssl_lprop::Matrix norm_uu; // UU part of norm
  ssl_lprop::Matrix norm_ul; // UL part of norm
  ssl_lprop::LabelMatrix y_u; // labels
  ssl_lprop::LabelMatrix y_l; // labels
  std::vector<int> labeled_nodes, unlabeled_nodes;
  std::vector<int> node_index_v;
};

void LP::clear()
{
  // clear
}
bool LP::read (const std::string mfilename,
               const std::string lfilename)
{
  try {
    ssl_lprop::load_mat(trans, norm, mfilename);
    C_ = ssl_lprop::load_lab(labels, lfilename);
  } catch (const std::exception& e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    return false;
  }
  assert(trans.size() == labels.size());
  N_ = trans.size();
  L_ = 0;
  std::vector<int> unlabeled;
  for (int i=0; i<N_; i++) {
    if (labels[i] < 0) {
      unlabeled.push_back(i+1);
    } else {
      L_++;
    }
  }
  U_ = N_ - L_;
  node_index_v    = std::vector<int>(N_);
  labeled_nodes   = std::vector<int>(L_);
  unlabeled_nodes = std::vector<int>(U_);

  int l=0, u=0;
  for (int i=0; i<N_; i++) {
    if (labels[i] < 0) {
      node_index_v[i] = u;
      unlabeled_nodes[u++] = i;
    } else {
      node_index_v[i] = l;
      labeled_nodes[l++] = i;
    }
  }
  assert(l==L_);
  assert(u==U_);
  assert(static_cast<int>(unlabeled_nodes.size() == U_));
  assert(static_cast<int>(labeled_nodes.size() == L_));
  assert(U_ == static_cast<int>(unlabeled.size()));

  try {
    if (N_ <= 0) throw std::runtime_error ("N is invalid.");
    if (L_ <= 0) throw std::runtime_error ("L is invalid.");
    if (U_ <= 0) throw std::runtime_error ("U is invalid.");
    if (N_ != L_ + U_)
      throw std::runtime_error ("matrix and labels are invalid.");
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return false;
  }

  norm_uu = ssl_lprop::Matrix(U_, ssl_lprop::Array());
  norm_ul = ssl_lprop::Matrix(U_, ssl_lprop::Array());
  load_submatrix(norm, norm_uu, norm_ul,
                 U_, L_, unlabeled_nodes, labeled_nodes,
                 labels);
  y_u = ssl_lprop::LabelMatrix(C_, std::vector<double>(U_, 0.0));
  y_l = ssl_lprop::LabelMatrix(C_, std::vector<double>(L_, 0.0));
  for (unsigned int i=0; i<labeled_nodes.size(); i++) {
    const int node_index = labeled_nodes[i];
    assert(labels[node_index] != 0);
    if (labels[node_index] > 0) {
      const int label_index = labels[node_index] - 1;
      y_l[label_index][i] = 1.0;
    }
  }

  return true;
}
bool LP::train (const int max_iter)
{
  try {
    if (N_ <= 0) throw std::runtime_error ("N is invalid.");
    if (L_ <= 0) throw std::runtime_error ("L is invalid.");
    if (U_ <= 0) throw std::runtime_error ("U is invalid.");
    if (eps_ <= 1.0e-200) throw std::runtime_error ("eps is too small.");
    if (N_ != L_ + U_)
      throw std::runtime_error ("matrix and labels are invalid.");
  } catch (const std::exception& e) {
    std::stringstream ss;
    ss << "LP::train() says: " << e.what();
    throw std::runtime_error (ss.str());
  }

  std::cout << "Number of nodes:              " << N_ << std::endl;
  std::cout << "Number of labeled nodes:      " << L_ << std::endl;
  std::cout << "Number of unlabeled nodes:    " << U_ << std::endl;
  std::cout << "eps:                          " << eps_ << std::endl;
  std::cout << "max iteration:                " << max_iter << std::endl;

  for (int iter = 1; iter < max_iter; iter++) {
    double err = 0.0;
    std::vector<std::vector<double> > y_ret(C_, std::vector<double>(U_, 0.0));
    for (unsigned int i=0; i < U_; i++) {
      for (unsigned int c=0; c<C_; c++) {
        const ssl_lprop::Array arry = norm_ul[i];
        const int edge_sz = arry.size();
        for (int j=0; j<edge_sz; j++) {
          const double w = arry[j].weight;
          const int src_index = arry[j].node - 1;
          const double y_u_w = y_l[c][node_index_v[src_index]];
          if (y_u_w < 1.0e-200 || w < 1.0e-200) continue;
          y_ret[c][i] += w * y_u_w;
        }
      }
    }

    for (unsigned int i=0; i < U_; i++) {
      for (int c=0; c<C_; c++) {
        const ssl_lprop::Array arry = norm_uu[i];
        const int edge_sz = arry.size();
        for (int j=0; j<edge_sz; j++) {
          const double w = arry[j].weight;
          const int src_index = arry[j].node - 1;
          const double y_u_w = y_u[c][node_index_v[src_index]];
          if (y_u_w < 1.0e-200 || w < 1.0e-200) continue;
          y_ret[c][i] += w * y_u_w;
        }
        err += y_ret[c][i] > y_u[c][i] ?
            y_ret[c][i]-y_u[c][i] : y_u[c][i]-y_ret[c][i];
      }
    }
    if (iter % 400 == 1) {
      std::cout << std::endl;
      if (iter != 1) std::cout << " error: " << err << std::endl;
    }
    if (iter % 10 == 1) std::cout << ".";
    y_u = y_ret;
    if (eps_ > err) {
      std::cout << std::endl << iter << " iteration done." << std::endl;
      break;
    }
  }

  return true;
}
bool LP::write (const char* filename,
                const unsigned int prec)
{
  //
}
void LP::show (const unsigned int prec)
{
  std::cout << "========================================" << std::endl;
  std::stringstream ss;
  ss.setf(std::ios::fixed, std::ios::floatfield);
  ss.precision(prec);

  for (int i=0; i<N_; i++) {
    if (labels[i] >= 0) {
      ss << "L: ";
      for (int c=0; c<C_; c++) {
        if (c!=0) ss << " ";
        ss << y_l[c][node_index_v[i]];
      }
      ss << std::endl;
    } else {
      ss << "U: ";
      for (int c=0; c<C_; c++) {
        if (c!=0) ss << " ";
        ss << y_u[c][node_index_v[i]];
      }
      ss << std::endl;
    }
  }
  std::cout << ss.str();
  ss.str("");
  ss.clear();

  for (int i=0; i<N_; i++) {
    int argmax = 0;
    double argmax_val = -1.0;
    for (int c=0; c<C_; c++) {
      if (labels[i] >= 0) {
        for (int c=0; c<C_; c++) {
          if (argmax_val < y_l[c][node_index_v[i]]) {
            argmax_val = y_l[c][node_index_v[i]];
            argmax = c;
          }
        }
      } else {
        for (int c=0; c<C_; c++) {
          if (argmax_val < y_u[c][node_index_v[i]]) {
            argmax_val = y_u[c][node_index_v[i]];
            argmax = c;
          }
        }
      }
    }
    ss << argmax+1 << std::endl;
  }

  std::cout << ss.str();
}

} // end of graph namespace

int main (int argc, char** argv)
{
  std::string input_matrix;
  std::string input_labels;
  std::string result;
  unsigned int max_iter = ITER_DEFAULT;
  unsigned int prec     = PREC_DEFAULT;
  double eps = EPS_DEFAULT;

  int opt;
  extern char *optarg;
  while ((opt = getopt(argc,argv,"e:p:m:i:l:r:h")) != -1) {
    switch(opt) {
      case 'm': max_iter = atoi(optarg); break;
      case 'e': eps = atof(optarg); break;
      case 'p': prec = atoi(optarg); break;
      case 'i': input_matrix = std::string(optarg); break;
      case 'l': input_labels = std::string(optarg); break;
      case 'r': result = std::string(optarg); break;
      case 'h': std::cout << "./lprop" << HELP << std::endl;
        return EXIT_FAILURE;
      default:
        std::cout << "./lprop" << HELP << std::endl; return EXIT_FAILURE;
    }
  }

  try { // validation
    if (input_matrix.empty()) throw std::runtime_error ("no matrix input");
    if (input_labels.empty()) throw std::runtime_error ("no label input");
    if (result.empty()) throw std::runtime_error ("specify output file");
  } catch (const std::exception& e) {
    std::cerr << "ERROR: main() says: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  try { // execute label propagation
    ssl_lprop::LP lprop(eps);
    lprop.read(input_matrix, input_labels);
    lprop.train(max_iter);
    lprop.write(argv[argc-1], prec);
    lprop.show(prec);
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}