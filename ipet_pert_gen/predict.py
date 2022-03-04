# usage python predict.py <num_samples> <dev_id> <chain> <generation> > vectors/smrtthings.csv 2>/dev/null
# python predict.py 25000 smrtthings 2 3 > vectors/smrtthings.csv 2>/dev/null

import pandas as pd
import numpy as np
import argparse
import os
import logging
import tensorflow as tf
from tensorflow.keras.utils import get_custom_objects
from numpy.random import randn

tf.get_logger().setLevel(logging.ERROR)
os.environ["CUDA_VISIBLE_DEVICES"] = "-1"
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3' 

def negative_categorical_crossentropy(yTrue, yPred):
    return 0.0 - K.categorical_crossentropy(yTrue, yPred)

get_custom_objects().update({'negative_categorical_crossentropy': negative_categorical_crossentropy})

def generate_latent_points(n_samples,latent_dim=20):
    x_input = randn(latent_dim * n_samples)
    x_input = x_input.reshape(n_samples, latent_dim)
    return x_input

def generate_perturbations(number_of_perturbations, generator):
    x = generate_latent_points(number_of_perturbations, latent_dim=20)
    return tf.round(generator.predict([x]))

def load_gen(device, batch, epoch):
    model_path = 'Models/Pert/Bandwidth/4_100/Generators/Batch'+str(batch)+'/Epoch_'+str(epoch)+'/generator_'+str(device) + '.0'
    model = tf.keras.models.load_model(model_path,custom_objects = get_custom_objects())
    return model

# create device_name, device_id mapping
device_name = ['smrtthings','echo','netatwel','tpcloudcmra','smgsmrtcam','dropcam','instcama',
               'instcamb','wthbabymtr','belkinswch','tpsmrtplg','ihome','belkinmotn','nestsmkalrm',
               'netatwthst','wthsmrtscl','bldsgrmtr','wthaura','lifxblb','tribyspk','pxstrframe','hpprint',
               'smgglxtb','nstdropcm','andrphb','laptop','macbook','andrph','iphone','maciphone']

device_name_id = dict(zip(device_name, range(30)))

parser = argparse.ArgumentParser()
parser.add_argument("num_samples", type=int, help="The number of perturbation vectors you want to generate")
parser.add_argument("dev", type=str, choices = device_name, help="The Device ID for which you want the perturbation vector for")
parser.add_argument("bat", type=int, help="The batch number you want your geneator from ")
parser.add_argument("gen", type=int ,help="The generation from which you want to generate perturbations")
args = parser.parse_args()

device = device_name_id[args.dev]
batch = args.bat
epoch = args.gen
generator = load_gen(device, batch, epoch)
perturbations = generate_perturbations(args.num_samples, generator)

temp = perturbations.numpy()
temp = temp.reshape(-1,4)
temp = temp.astype('str')
for arr in temp:
    print(','.join(arr))
