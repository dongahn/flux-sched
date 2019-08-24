#!/bin/bash

flux jobspec srun -n 1 sleep 0 | flux job submit -
