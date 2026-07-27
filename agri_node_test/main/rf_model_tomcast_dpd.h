


    // !!! This file is generated using emlearn !!!

    #include <stdint.h>
    

static inline int32_t rf_tomcast_dpd_tree_0(const int16_t *features, int32_t features_length) {
          if (features[1] < 20) {
              if (features[0] < 12) {
                  return 0;
              } else {
                  if (features[1] < 15) {
                      if (features[1] < 6) {
                          if (features[1] < 3) {
                              return 0;
                          } else {
                              if (features[0] < 17) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[0] < 17) {
                              if (features[1] < 9) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  } else {
                      if (features[0] < 17) {
                          return 2;
                      } else {
                          return 3;
                      }
                  }
              }
          } else {
              if (features[0] < 18) {
                  if (features[1] < 23) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  } else {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[1] < 22) {
                      return 3;
                  } else {
                      return 4;
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_1(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[0] < 18) {
                  if (features[0] < 16) {
                      if (features[0] < 15) {
                          if (features[1] < 20) {
                              if (features[1] < 15) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          } else {
                              return 3;
                          }
                      } else {
                          if (features[0] < 16) {
                              if (features[1] < 20) {
                                  return 1;
                              } else {
                                  return 3;
                              }
                          } else {
                              if (features[0] < 16) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      }
                  } else {
                      if (features[1] < 15) {
                          if (features[0] < 17) {
                              if (features[0] < 16) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          }
                      } else {
                          if (features[0] < 17) {
                              if (features[1] < 20) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          } else {
                              return 3;
                          }
                      }
                  }
              } else {
                  if (features[0] < 18) {
                      if (features[0] < 18) {
                          if (features[1] < 22) {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          } else {
                              return 4;
                          }
                      } else {
                          return 4;
                      }
                  } else {
                      if (features[1] < 22) {
                          if (features[0] < 19) {
                              if (features[1] < 15) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          } else {
                              if (features[0] < 19) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          }
                      } else {
                          return 4;
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_2(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[1] < 20) {
                  if (features[1] < 15) {
                      if (features[1] < 6) {
                          if (features[0] < 17) {
                              return 0;
                          } else {
                              if (features[1] < 3) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[0] < 17) {
                              return 1;
                          } else {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  } else {
                      if (features[0] < 17) {
                          return 2;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[0] < 18) {
                      return 3;
                  } else {
                      if (features[1] < 22) {
                          return 3;
                      } else {
                          return 4;
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_3(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[0] < 18) {
                  if (features[1] < 15) {
                      if (features[1] < 6) {
                          if (features[0] < 17) {
                              return 0;
                          } else {
                              if (features[0] < 18) {
                                  return 1;
                              } else {
                                  return 0;
                              }
                          }
                      } else {
                          if (features[1] < 10) {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[0] < 17) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  } else {
                      if (features[1] < 20) {
                          if (features[1] < 16) {
                              return 2;
                          } else {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[0] < 18) {
                      if (features[0] < 18) {
                          if (features[1] < 22) {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          } else {
                              return 4;
                          }
                      } else {
                          return 4;
                      }
                  } else {
                      if (features[0] < 18) {
                          if (features[0] < 18) {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 18) {
                                  return 3;
                              } else {
                                  return 2;
                              }
                          }
                      } else {
                          if (features[0] < 19) {
                              if (features[1] < 22) {
                                  return 2;
                              } else {
                                  return 4;
                              }
                          } else {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_4(const int16_t *features, int32_t features_length) {
          if (features[1] < 20) {
              if (features[1] < 15) {
                  if (features[1] < 6) {
                      if (features[0] < 17) {
                          return 0;
                      } else {
                          if (features[1] < 3) {
                              return 0;
                          } else {
                              return 1;
                          }
                      }
                  } else {
                      if (features[1] < 8) {
                          if (features[1] < 7) {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[0] < 12) {
                              return 0;
                          } else {
                              if (features[0] < 17) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  }
              } else {
                  if (features[1] < 17) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          if (features[1] < 16) {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          } else {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      }
                  } else {
                      if (features[1] < 19) {
                          if (features[1] < 18) {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 2;
                              }
                          }
                      } else {
                          if (features[0] < 12) {
                              return 0;
                          } else {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      }
                  }
              }
          } else {
              if (features[0] < 18) {
                  if (features[0] < 12) {
                      return 0;
                  } else {
                      return 3;
                  }
              } else {
                  if (features[1] < 22) {
                      return 3;
                  } else {
                      return 4;
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_5(const int16_t *features, int32_t features_length) {
          if (features[1] < 20) {
              if (features[0] < 12) {
                  return 0;
              } else {
                  if (features[1] < 15) {
                      if (features[1] < 6) {
                          if (features[1] < 3) {
                              return 0;
                          } else {
                              if (features[1] < 5) {
                                  return 0;
                              } else {
                                  return 0;
                              }
                          }
                      } else {
                          if (features[0] < 17) {
                              if (features[0] < 13) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[0] < 19) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  } else {
                      if (features[1] < 19) {
                          if (features[1] < 18) {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          } else {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      } else {
                          if (features[0] < 18) {
                              return 2;
                          } else {
                              return 3;
                          }
                      }
                  }
              }
          } else {
              if (features[1] < 22) {
                  if (features[1] < 21) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  } else {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[0] < 17) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  } else {
                      return 4;
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_6(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[0] < 18) {
                  if (features[0] < 16) {
                      if (features[1] < 20) {
                          if (features[0] < 15) {
                              if (features[0] < 15) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 15) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          return 3;
                      }
                  } else {
                      if (features[1] < 15) {
                          if (features[1] < 6) {
                              if (features[1] < 3) {
                                  return 0;
                              } else {
                                  return 0;
                              }
                          } else {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[0] < 17) {
                              if (features[1] < 20) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          } else {
                              return 3;
                          }
                      }
                  }
              } else {
                  if (features[0] < 18) {
                      if (features[0] < 18) {
                          if (features[0] < 18) {
                              if (features[0] < 18) {
                                  return 4;
                              } else {
                                  return 4;
                              }
                          } else {
                              if (features[1] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      } else {
                          return 4;
                      }
                  } else {
                      if (features[0] < 18) {
                          if (features[0] < 18) {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          } else {
                              return 3;
                          }
                      } else {
                          if (features[0] < 19) {
                              if (features[0] < 19) {
                                  return 4;
                              } else {
                                  return 4;
                              }
                          } else {
                              if (features[0] < 19) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_7(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[1] < 20) {
                  if (features[0] < 17) {
                      if (features[1] < 15) {
                          if (features[1] < 6) {
                              return 0;
                          } else {
                              if (features[0] < 13) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          return 2;
                      }
                  } else {
                      if (features[0] < 19) {
                          if (features[1] < 15) {
                              if (features[0] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      } else {
                          if (features[1] < 8) {
                              if (features[0] < 20) {
                                  return 1;
                              } else {
                                  return 0;
                              }
                          } else {
                              if (features[0] < 19) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  }
              } else {
                  if (features[0] < 18) {
                      return 3;
                  } else {
                      if (features[1] < 22) {
                          return 3;
                      } else {
                          return 4;
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_8(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[0] < 18) {
                  if (features[0] < 16) {
                      if (features[1] < 20) {
                          if (features[0] < 15) {
                              if (features[1] < 15) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 15) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          return 3;
                      }
                  } else {
                      if (features[1] < 15) {
                          if (features[1] < 6) {
                              if (features[0] < 17) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[1] < 20) {
                              if (features[1] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          } else {
                              return 3;
                          }
                      }
                  }
              } else {
                  if (features[1] < 22) {
                      if (features[0] < 18) {
                          if (features[1] < 15) {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          } else {
                              return 3;
                          }
                      } else {
                          if (features[0] < 19) {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 19) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  } else {
                      return 4;
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_9(const int16_t *features, int32_t features_length) {
          if (features[1] < 20) {
              if (features[0] < 12) {
                  return 0;
              } else {
                  if (features[1] < 15) {
                      if (features[1] < 6) {
                          if (features[1] < 3) {
                              return 0;
                          } else {
                              if (features[0] < 17) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[0] < 17) {
                              return 1;
                          } else {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  } else {
                      if (features[1] < 19) {
                          if (features[0] < 17) {
                              return 2;
                          } else {
                              return 3;
                          }
                      } else {
                          if (features[0] < 18) {
                              return 2;
                          } else {
                              return 3;
                          }
                      }
                  }
              }
          } else {
              if (features[1] < 22) {
                  if (features[1] < 21) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  } else {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[0] < 17) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  } else {
                      return 4;
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_10(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[1] < 20) {
                  if (features[1] < 15) {
                      if (features[1] < 6) {
                          if (features[0] < 17) {
                              return 0;
                          } else {
                              if (features[0] < 20) {
                                  return 1;
                              } else {
                                  return 0;
                              }
                          }
                      } else {
                          if (features[1] < 8) {
                              return 1;
                          } else {
                              if (features[1] < 11) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      }
                  } else {
                      if (features[0] < 17) {
                          return 2;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[1] < 22) {
                      return 3;
                  } else {
                      if (features[1] < 23) {
                          if (features[0] < 17) {
                              return 3;
                          } else {
                              return 4;
                          }
                      } else {
                          if (features[0] < 17) {
                              return 3;
                          } else {
                              return 4;
                          }
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_11(const int16_t *features, int32_t features_length) {
          if (features[1] < 20) {
              if (features[0] < 12) {
                  return 0;
              } else {
                  if (features[1] < 15) {
                      if (features[0] < 17) {
                          if (features[0] < 13) {
                              if (features[1] < 6) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[1] < 6) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[1] < 8) {
                              if (features[0] < 20) {
                                  return 1;
                              } else {
                                  return 0;
                              }
                          } else {
                              return 2;
                          }
                      }
                  } else {
                      if (features[0] < 17) {
                          return 2;
                      } else {
                          return 3;
                      }
                  }
              }
          } else {
              if (features[1] < 22) {
                  if (features[1] < 21) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  } else {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[1] < 23) {
                      if (features[0] < 17) {
                          if (features[0] < 13) {
                              return 0;
                          } else {
                              return 3;
                          }
                      } else {
                          return 4;
                      }
                  } else {
                      if (features[0] < 17) {
                          if (features[0] < 12) {
                              return 0;
                          } else {
                              return 3;
                          }
                      } else {
                          return 4;
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_12(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[1] < 20) {
                  if (features[1] < 15) {
                      if (features[1] < 6) {
                          if (features[1] < 3) {
                              return 0;
                          } else {
                              if (features[0] < 17) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[1] < 8) {
                              return 1;
                          } else {
                              if (features[1] < 11) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      }
                  } else {
                      if (features[1] < 19) {
                          if (features[1] < 18) {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          } else {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      } else {
                          if (features[0] < 18) {
                              return 2;
                          } else {
                              return 3;
                          }
                      }
                  }
              } else {
                  if (features[1] < 22) {
                      return 3;
                  } else {
                      if (features[0] < 17) {
                          return 3;
                      } else {
                          return 4;
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_13(const int16_t *features, int32_t features_length) {
          if (features[0] < 12) {
              return 0;
          } else {
              if (features[1] < 20) {
                  if (features[1] < 15) {
                      if (features[0] < 17) {
                          if (features[1] < 6) {
                              return 0;
                          } else {
                              if (features[0] < 13) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[0] < 20) {
                              if (features[1] < 8) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 21) {
                                  return 1;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  } else {
                      if (features[0] < 17) {
                          return 2;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[1] < 22) {
                      return 3;
                  } else {
                      if (features[1] < 23) {
                          if (features[0] < 17) {
                              return 3;
                          } else {
                              return 4;
                          }
                      } else {
                          if (features[0] < 17) {
                              return 3;
                          } else {
                              return 4;
                          }
                      }
                  }
              }
          }
        }
        

static inline int32_t rf_tomcast_dpd_tree_14(const int16_t *features, int32_t features_length) {
          if (features[1] < 20) {
              if (features[1] < 15) {
                  if (features[1] < 6) {
                      if (features[1] < 3) {
                          return 0;
                      } else {
                          if (features[1] < 5) {
                              if (features[1] < 4) {
                                  return 0;
                              } else {
                                  return 0;
                              }
                          } else {
                              if (features[0] < 17) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      }
                  } else {
                      if (features[1] < 8) {
                          if (features[1] < 7) {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      } else {
                          if (features[1] < 11) {
                              if (features[1] < 10) {
                                  return 1;
                              } else {
                                  return 1;
                              }
                          } else {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 1;
                              }
                          }
                      }
                  }
              } else {
                  if (features[1] < 16) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          if (features[0] < 18) {
                              return 2;
                          } else {
                              return 3;
                          }
                      }
                  } else {
                      if (features[1] < 17) {
                          if (features[0] < 12) {
                              return 0;
                          } else {
                              if (features[0] < 17) {
                                  return 2;
                              } else {
                                  return 3;
                              }
                          }
                      } else {
                          if (features[1] < 19) {
                              if (features[1] < 18) {
                                  return 2;
                              } else {
                                  return 2;
                              }
                          } else {
                              if (features[0] < 12) {
                                  return 0;
                              } else {
                                  return 2;
                              }
                          }
                      }
                  }
              }
          } else {
              if (features[1] < 22) {
                  if (features[0] < 12) {
                      return 0;
                  } else {
                      return 3;
                  }
              } else {
                  if (features[0] < 17) {
                      if (features[0] < 12) {
                          return 0;
                      } else {
                          return 3;
                      }
                  } else {
                      return 4;
                  }
              }
          }
        }
        

int32_t rf_tomcast_dpd_predict(const int16_t *features, int32_t features_length) {

        int32_t votes[5] = {0,};
        int32_t _class = -1;

        _class = rf_tomcast_dpd_tree_0(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_1(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_2(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_3(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_4(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_5(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_6(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_7(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_8(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_9(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_10(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_11(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_12(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_13(features, features_length); votes[_class] += 1;
    _class = rf_tomcast_dpd_tree_14(features, features_length); votes[_class] += 1;
    
        int32_t most_voted_class = -1;
        int32_t most_voted_votes = 0;
        for (int32_t i=0; i<5; i++) {

            if (votes[i] > most_voted_votes) {
                most_voted_class = i;
                most_voted_votes = votes[i];
            }
        }
        return most_voted_class;
    }
    

int rf_tomcast_dpd_predict_proba(const int16_t *features, int32_t features_length, float *out, int out_length) {

        int32_t _class = -1;

        for (int i=0; i<out_length; i++) {
            out[i] = 0.0f;
        }

        _class = rf_tomcast_dpd_tree_0(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_1(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_2(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_3(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_4(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_5(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_6(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_7(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_8(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_9(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_10(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_11(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_12(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_13(features, features_length); out[_class] += 1.0f;
    _class = rf_tomcast_dpd_tree_14(features, features_length); out[_class] += 1.0f;
    
        // compute mean
        for (int i=0; i<out_length; i++) {
            out[i] = out[i] / 15;
        }
        return 0;
    }
    