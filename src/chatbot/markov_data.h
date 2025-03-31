#ifndef MARKOV_DATA_H
#define MARKOV_DATA_H

/* Load static training data into the Markov model */
void LoadStaticTrainingData(void);

/* Load dynamic system-specific training data into the Markov model */
void LoadDynamicTrainingData(void);

/* Load all training data into the Markov model */
void LoadTrainingData(void);

#endif /* MARKOV_DATA_H */