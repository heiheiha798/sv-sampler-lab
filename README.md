运行指令

```bash
bash build.sh
./evaluate.sh all
```

预期结果：70 + 4 + 4 + 3 + 4 + 9 = 94

```bash
Testing basic
Runtimes runninng basic/0.json
.822 .837 .715 .825 .808 .849 .811 .808 .808 .758
Max: 0.849, 8-th: 0.825, Median: 0.8095000000000001
OK
Runtimes runninng basic/1.json
2.732 2.561 2.908 2.667 2.731 2.495 2.693 2.993 2.726 2.532
Max: 2.993, 8-th: 2.732, Median: 2.7095000000000002
OK
Runtimes runninng basic/2.json
5.547 5.467 5.724 5.417 5.544 5.567 5.558 5.489 5.553 5.417
Max: 5.724, 8-th: 5.558, Median: 5.5455
OK
Runtimes runninng basic/3.json
4.091 3.702 3.923 4.107 4.039 4.116 4.143 3.616 3.985 3.624
Max: 4.143, 8-th: 4.107, Median: 4.012
OK
Runtimes runninng basic/4.json
7.375 7.642 7.180 7.777 7.586 7.988 7.949 7.462 8.121 7.646
Max: 8.121, 8-th: 7.949, Median: 7.644
OK
Runtimes runninng basic/5.json
1.798 1.775 1.924 1.756 1.763 1.761 1.824 1.879 1.730 1.952
Max: 1.952, 8-th: 1.879, Median: 1.7865
OK
Runtimes runninng basic/6.json
20.672 21.009 19.743 19.750 20.636 20.620 19.635 19.664 19.482 20.666
Max: 21.009, 8-th: 20.666, Median: 20.185000000000002
OK
Runtimes runninng basic/7.json
19.794 18.040 18.354 18.272 19.876 19.789 18.245 17.910 19.751 18.178
Max: 19.876, 8-th: 19.789, Median: 18.313
OK
Runtimes runninng basic/8.json
14.936 15.297 13.528 15.730 13.433 14.670 13.613 14.508 14.988 13.649
Max: 15.73, 8-th: 14.988, Median: 14.588999999999999
OK
Runtimes runninng basic/9.json
2.538 2.539 2.725 2.385 2.460 2.541 2.631 2.513 2.324 2.630
Max: 2.725, 8-th: 2.63, Median: 2.5385
OK
Runtimes runninng basic/10.json
12.483 12.564 12.652 11.998 13.861 13.347 12.632 13.051 14.443 11.801
Max: 14.443, 8-th: 13.347, Median: 12.642
OK
Runtimes runninng basic/11.json
4.864 3.969 4.712 4.554 4.898 4.546 4.602 4.770 4.070 4.023
Max: 4.898, 8-th: 4.77, Median: 4.578
OK
Runtimes runninng basic/12.json
4.085 3.879 3.877 3.990 4.453 4.160 3.822 4.010 4.246 4.042
Max: 4.453, 8-th: 4.16, Median: 4.026
OK
Runtimes runninng basic/13.json
2.083 1.988 2.175 1.971 1.980 1.979 1.957 1.991 2.058 2.048
Max: 2.175, 8-th: 2.058, Median: 1.9895
OK
Runtimes runninng basic/14.json
.721 .766 .747 .781 .711 .714 .727 .704 .696 .702
Max: 0.781, 8-th: 0.747, Median: 0.7175
OK
Runtimes runninng basic/15.json
.624 .518 .523 .571 .531 .630 .634 .541 .593 .537
Max: 0.634, 8-th: 0.624, Median: 0.556
OK
Runtimes runninng basic/16.json
1.740 1.755 1.824 2.172 1.800 1.763 2.070 1.887 1.773 1.744
Max: 2.172, 8-th: 1.887, Median: 1.7865
OK
Runtimes runninng basic/17.json
.738 .775 .808 .657 .759 .699 .718 .674 .712 .693
Max: 0.808, 8-th: 0.759, Median: 0.715
OK
Runtimes runninng basic/18.json
.766 .747 .812 .791 .764 .720 .772 .792 .787 .792
Max: 0.812, 8-th: 0.792, Median: 0.7795000000000001
OK
Runtimes runninng basic/19.json
4.619 4.488 4.572 4.703 4.603 4.683 4.402 4.381 4.476 4.732
Max: 4.732, 8-th: 4.683, Median: 4.5875
OK
Score basic: 70.000

Testing opt1
Runtimes runninng opt1/0.json
3.125 3.261 3.351 3.301 3.004 2.850 3.279 3.354 3.275 3.260
Max: 3.354, 8-th: 3.301, Median: 3.268
OK
Runtimes runninng opt1/1.json
10.079 10.077 10.105 10.109 9.455 9.795 9.572 10.090 9.965 10.260
Max: 10.26, 8-th: 10.105, Median: 10.078
OK
Score opt1: 4.000

Testing opt2
Runtimes runninng opt2/0.json
4.201 4.145 4.100 3.289 4.255 4.189 4.119 4.204 4.089 4.042
Max: 4.255, 8-th: 4.201, Median: 4.132
OK
Runtimes runninng opt2/1.json
7.320 8.622 7.133 9.089 8.107 7.372 7.968 7.265 8.570 8.426
Max: 9.089, 8-th: 8.57, Median: 8.0375
OK
Score opt2: 4.000

Testing opt3
Runtimes runninng opt3/0.json
3.093 3.251 3.249 2.608 2.708 2.603 2.987 3.297 2.673 2.532
Max: 3.297, 8-th: 3.249, Median: 2.8475
OK
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Runtimes runninng opt3/1.json
30 30 30 30 30 30 30 30 30 30
Max: 30.0, 8-th: 30.0, Median: 30.0
OK
Score opt3: 3.000

Testing opt4
Runtimes runninng opt4/0.json
50.671 48.987 47.563 47.808 49.688 47.960 48.163 51.521 48.701 50.600
Max: 51.521, 8-th: 50.6, Median: 48.844
OK
Score opt4: 4.000

Testing opt5
Runtimes runninng opt5/0.json
4.060 3.533 3.607 4.124 4.280 4.221 3.426 4.114 4.010 3.400
Max: 4.28, 8-th: 4.124, Median: 4.035
OK
Runtimes runninng opt5/1.json
13.188 13.109 12.871 13.111 13.241 12.734 13.251 13.030 15.119 12.679
Max: 15.119, 8-th: 13.241, Median: 13.11
OK
Runtimes runninng opt5/2.json
10.522 11.850 11.303 11.897 11.233 11.071 11.049 10.740 10.335 10.920
Max: 11.897, 8-th: 11.303, Median: 11.059999999999999
OK
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
Timeout.
1000
Runtimes runninng opt5/3.json
50 50 50 49.367 50 50 50 50 50 50
Max: 50.0, 8-th: 50.0, Median: 50.0
OK
Score opt5: 9.000
```

注：运行结果生成在 sv-sampler-lab/_run 文件夹下