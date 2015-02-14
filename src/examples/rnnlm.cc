#include "cnn/edges.h"
#include "cnn/cnn.h"
#include "cnn/training.h"
#include "cnn/timing.h"
#include "cnn/rnn.h"
#include "cnn/lstm.h"

#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;
using namespace cnn;

unsigned LAYERS = 2;
unsigned INPUT_DIM = 10;
unsigned HIDDEN_DIM = 50;
unsigned VOCAB_SIZE = 32;

int main(int argc, char** argv) {
  srand(time(0));
  if (argc != 2) {
    cerr << "Usage: " << argv[0] << " text.ints\n";
    return 1;
  }
  vector<vector<unsigned>> corpus;
  string line;
  ifstream in(argv[1]);
  while(getline(in, line)) {
    istringstream is(line);
    vector<unsigned> x;
    unsigned v;
    while (is >> v) {
      assert(v < VOCAB_SIZE);
      x.push_back(v);
    }
    corpus.push_back(x);
  }

  Model model;
  SimpleSGDTrainer sgd(&model);

  // parameters
  LookupParameters* p_c = model.add_lookup_parameters(VOCAB_SIZE, Dim(INPUT_DIM, 1)); 
  Parameters* p_R = model.add_parameters(Dim(VOCAB_SIZE, HIDDEN_DIM));
  Parameters* p_bias = model.add_parameters(Dim(VOCAB_SIZE, 1));
  //RNNBuilder rnn(LAYERS, INPUT_DIM, HIDDEN_DIM, &model);
  LSTMBuilder rnn(LAYERS, INPUT_DIM, HIDDEN_DIM, &model);

  for (unsigned iter = 0; iter < 1000; ++iter) {
    Timer iteration("epoch completed in");
    double loss = 0;
    unsigned lines = 0;
    unsigned chars = 0;
    for (auto& sent : corpus) {
      Hypergraph hg;
      rnn.new_graph();  // reset RNN builder for new graph
      rnn.add_parameter_edges(&hg);  // add variables for its parameters
      VariableIndex i_R = hg.add_parameter(p_R); // hidden -> word rep parameter
      VariableIndex i_bias = hg.add_parameter(p_bias);  // word bias
      vector<VariableIndex> errs;
      const unsigned slen = sent.size() - 1;
      for (unsigned t = 0; t < slen; ++t) {
        VariableIndex i_x_t = hg.add_lookup(p_c, sent[t]); // input
        // y_t = RNN(x_t)
        VariableIndex i_y_t = rnn.add_input(i_x_t, &hg);
        // r_t = bias + R * y_t
        VariableIndex i_r_t = hg.add_function<Multilinear>({i_bias, i_R, i_y_t});
        // ydist = softmax(r_t)
        VariableIndex i_ydist = hg.add_function<LogSoftmax>({i_r_t});  
        VariableIndex i_ytrue = hg.add_input(sent[t+1]);
        errs.push_back(hg.add_function<PickElement>({i_ydist, i_ytrue}));
        chars++;
      }
      VariableIndex i_nerr = hg.add_function<Sum>(errs);
      hg.add_function<Negate>({i_nerr});
      loss += hg.forward()(0,0);
      hg.backward();
      sgd.update(0.5 / slen);
      ++lines;
      if (lines == 1000) break;
    }
    cerr << "E = " << (loss / chars);
  }
}
