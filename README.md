This is a program that can find wordsquares and all other wordshapes (triangles, cubes, anything you can think of). Look in topologies for examples on how to define a topology, dutch wordlists are provided. I believe it's the fastest of its kind that is readibly available.

Example run (on i7-4712HQ, g++ fieldfinder.cpp -O3 -std=c++14 -ofieldfinder -pthread -march=native)

$ time ./fieldfinder topologies/perfect/square8_perfect words_medium 
 capitool aganippe patenten inenting tintelde optilden opendeed lengende
 capitool aganippe patenten inenting tintelde optillen opendeed lengende
 machadoi aerofoon cremeert hompelde afeetten doeltjes oordeele intenser
 cathedra agrarier tranerig handbike erebogen dirigent reikende argentea
 chartaal humorale amusante rossiger trailers aangezet alterego leerstof
 chartaal humorale amusante rossiger trainers aangezet alterego leerstof
 schouwen cdopname hoplites oplevert universe watertor emersoni nesterig
 chemobom humanere emachten macleari onheilen betaling orerende meningen
...snipped 40 lines....
 topclubs oproeien premetro computer leeuwrik uittrekt bereikte snorkten
 bossaami ontnamen steenbed snellere aanlaten ambetant meerende indenter

real	0m2.212s
user	0m10.787s
sys	0m0.007s
