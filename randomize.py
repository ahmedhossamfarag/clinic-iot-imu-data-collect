import pandas as pd

df = pd.read_csv('data/data_set.csv', header=None)

for _ in range(10):
    df = df.sample(frac=1).reset_index(drop=True)

df.to_csv('data_set.csv', index=False, header=False)