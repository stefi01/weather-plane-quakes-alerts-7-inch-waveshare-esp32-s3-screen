#pragma once
/* Orbits from JPL. The screen steps these forward. Distances are approximate. */
struct Rock {
  const char *name;
  const char *kind;
  const char *note;
  float H;
  double e, a, inc, om, w, ma, epochJd;
};
static const Rock ROCKS[] = {
  {"Ceres", "asteroid", "the largest asteroid", 3.3f, 0.07969229514816586, 2.765552595034094, 10.58802780183462, 80.24862682043221, 73.29421453021587, 274.4193463761342, 2461200.5},
  {"Vesta", "asteroid", "often the brightest asteroid", 3.2f, 0.09020374382834395, 2.361365965127599, 7.143925545058711, 103.701293265032, 151.4686478221564, 81.19015607686903, 2461200.5},
  {"Pallas", "asteroid", "a tilted orbit", 4.1f, 0.2307000995648547, 2.769559010737709, 34.93279321851542, 172.8866193357694, 310.9699161652136, 254.2496521742734, 2461200.5},
  {"Juno", "asteroid", "one of the first four found", 5.3f, 0.2556999836681878, 2.670989527103278, 12.98659236598085, 169.8115953492418, 247.8950743075613, 262.7322944883855, 2461200.5},
  {"Encke", "comet", "orbit about 3.3 years. nucleus about 3 miles across. water ice and dust. brightest around Feb 2027", 99.0f, 0.846940430888432, 2.218624763741819, 11.33675914978793, 334.0171400057891, 187.2881998732444, 36.82518175471338, 2460363.5},
  {"Hartley-IRAS", "comet", "orbit about 21 years. nucleus size unknown. water ice and dust. faint", 99.0f, 0.8345128081503957, 7.727126616599012, 95.72344810775139, 1.43387246469738, 46.91020211271879, 246.673331857998, 2458903.5},
};
